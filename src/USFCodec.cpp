/*
 *      Copyright (C) 2014 Arne Morten Kvarving
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "libXBMC_addon.h"
#include "usf.h"
#include "psflib.h"
#include <iostream>

extern "C" {
#include <stdio.h>
#include <stdint.h>

#include "kodi_audiodec_dll.h"

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_OK;
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

struct USFContext {
  char* state;
  int64_t len;
  int sample_rate;
  int64_t pos;
  std::string title;
  std::string artist;
};

int usf_load(void* ctx, const uint8_t*, size_t exe_size,
             const uint8_t* data, size_t size)
{
  if (exe_size > 0)
    return -1;
  usf_upload_section(ctx, data, size);
  return 0;
}

static void * psf_file_fopen( const char * uri )
{
  return XBMC->OpenFile(uri, 0);
}

static size_t psf_file_fread( void * buffer, size_t size, size_t count, void * handle )
{
  return XBMC->ReadFile(handle, buffer, size*count);
}

static int psf_file_fseek( void * handle, int64_t offset, int whence )
{
  return XBMC->SeekFile(handle, offset, whence) > -1?0:-1;
}

static int psf_file_fclose( void * handle )
{
  XBMC->CloseFile(handle);

  return 0;
}

static long psf_file_ftell( void * handle )
{
  return XBMC->GetFilePosition(handle);
}

const psf_file_callbacks psf_file_system =
{
  "\\/",
  psf_file_fopen,
  psf_file_fread,
  psf_file_fseek,
  psf_file_fclose,
  psf_file_ftell
};

#define BORK_TIME 0xC0CAC01A
static unsigned long parse_time_crap(const char *input)
{
  if (!input) return BORK_TIME;
  int len = strlen(input);
  if (!len) return BORK_TIME;
  int value = 0;
  {
    int i;
    for (i = len - 1; i >= 0; i--)
    {
      if ((input[i] < '0' || input[i] > '9') && input[i] != ':' && input[i] != ',' && input[i] != '.')
      {
        return BORK_TIME;
      }
    }
  }
  std::string foo = input;
  char *bar = (char *) &foo[0];
  char *strs = bar + foo.size() - 1;
  while (strs > bar && (*strs >= '0' && *strs <= '9'))
  {
    strs--;
  }
  if (*strs == '.' || *strs == ',')
  {
    // fraction of a second
    strs++;
    if (strlen(strs) > 3) strs[3] = 0;
    value = atoi(strs);
    switch (strlen(strs))
    {
      case 1:
        value *= 100;
        break;
      case 2:
        value *= 10;
        break;
    }
    strs--;
    *strs = 0;
    strs--;
  }
  while (strs > bar && (*strs >= '0' && *strs <= '9'))
  {
    strs--;
  }
  // seconds
  if (*strs < '0' || *strs > '9') strs++;
  value += atoi(strs) * 1000;
  if (strs > bar)
  {
    strs--;
    *strs = 0;
    strs--;
    while (strs > bar && (*strs >= '0' && *strs <= '9'))
    {
      strs--;
    }
    if (*strs < '0' || *strs > '9') strs++;
    value += atoi(strs) * 60000;
    if (strs > bar)
    {
      strs--;
      *strs = 0;
      strs--;
      while (strs > bar && (*strs >= '0' && *strs <= '9'))
      {
        strs--;
      }
      value += atoi(strs) * 3600000;
    }
  }
  return value;
}

static int psf_info_meta(void* context,
                         const char* name, const char* value)
{
  USFContext* usf = (USFContext*)context;
  if (!strcasecmp(name, "length"))
    usf->len = parse_time_crap(value);
  if (!strcasecmp(name, "title"))
    usf->title = value;
  if (!strcasecmp(name, "artist"))
    usf->artist = value;

  return 0;
}
             
void* Init(const char* strFile, unsigned int filecache, int* channels,
           int* samplerate, int* bitspersample, int64_t* totaltime,
           int* bitrate, AEDataFormat* format, const AEChannel** channelinfo)
{
  USFContext* result = new USFContext;
  result->pos = 0;
  result->state = new char[usf_get_state_size()];
  usf_clear(result->state);
  if (psf_load(strFile, &psf_file_system, 0x21, 0, 0, psf_info_meta, result, 0) <= 0)
  {
    delete result->state;
    delete result;
    return NULL;
  }
  if (psf_load(strFile, &psf_file_system, 0x21, usf_load, result->state, 0, 0, 0) < 0)
  {
    delete result->state;
    delete result;
    return NULL;
  }
  *totaltime = result->len;
  usf_set_compare(result->state, 0);
  usf_set_fifo_full(result->state, 0);
  usf_set_hle_audio(result->state, 1);
  static enum AEChannel map[3] = {
    AE_CH_FL, AE_CH_FR, AE_CH_NULL
  };
  *format = AE_FMT_S16NE;
  *channelinfo = map;
  *channels = 2;

  *bitspersample = 16;
  *bitrate = 0.0;

  int32_t srate;
  usf_render(result->state, NULL, 0, &srate);
  *samplerate = result->sample_rate = srate;

  result->len = srate*4*(*totaltime)/1000;

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  USFContext* usf = (USFContext*)context;
  if (usf->pos >= usf->len)
    return 1;
  if (usf_render(usf->state, (int16_t*)pBuffer, size/4, &usf->sample_rate))
    return 1;
  usf->pos += size;
  *actualsize = size;
  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  USFContext* usf = (USFContext*)context;
  if (time*usf->sample_rate*4/1000 < usf->pos)
  {
    usf_restart(usf->state);
    usf->pos = 0;
  }
  
  int64_t left = time*usf->sample_rate*4/1000-usf->pos;
  while (left > 4096)
  {
    usf_render(usf->state, NULL, 1024, &usf->sample_rate);
    usf->pos += 4096;
    left -= 4096;
  }

  return usf->pos/(usf->sample_rate*4)*1000;
}

bool DeInit(void* context)
{
  if (!context)
    return true;

  USFContext* usf = (USFContext*)context;
  usf_shutdown(usf->state);
  delete[] usf->state;
  delete usf;

  return true;
}

bool ReadTag(const char* strFile, char* title, char* artist, int* length)
{
  USFContext* usf = new USFContext;

  if (psf_load(strFile, &psf_file_system, 0x21, 0, 0, psf_info_meta, usf, 0) <= 0)
  {
    delete usf;
    return false;
  }

  strcpy(title, usf->title.c_str());
  strcpy(artist, usf->artist.c_str());
  *length = usf->len/1000;

  delete usf;
  return true;
}

int TrackCount(const char* strFile)
{
  return 1;
}
}
