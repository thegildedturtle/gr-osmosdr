/* -*- c++ -*- */
/*
 * Copyright 2015 SDRplay Ltd <support@sdrplay.com>
 * Copyright 2012 Dimitri Stolnikov <horiz0n@gmx.net>
 * Copyright 2012 Steve Markgraf <steve@steve-m.de>
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sdrplay_source_c.h"
#include <gnuradio/io_signature.h>
#include "osmosdr/source.h"

#include <boost/assign.hpp>
#include <boost/format.hpp>

#include <stdexcept>
#include <iostream>
#include <stdio.h>
#include <math.h>

#include <mirsdrapi-rsp.h>

#include "arg_helpers.h"

#define MAX_SUPPORTED_DEVICES   4

struct sdrplay_dev
{
   int gRdB;
   double gain_dB;
   double fsHz;
   double rfHz;
   mir_sdr_Bw_MHzT bwType;
   mir_sdr_If_kHzT ifType;
   int samplesPerPacket;
   int maxGain;
   int minGain;
   int dcMode;
};

using namespace boost::assign;

#define BYTES_PER_SAMPLE  4 // sdrplay device delivers 16 bit signed IQ data
                            // containing 12 bits of information

#define SDRPLAY_AM_MIN     150e3
#define SDRPLAY_AM_MAX      30e6
#define SDRPLAY_FM_MIN      64e6
#define SDRPLAY_FM_MAX     108e6
#define SDRPLAY_B3_MIN     162e6
#define SDRPLAY_B3_MAX     240e6
#define SDRPLAY_B45_MIN    470e6
#define SDRPLAY_B45_MAX    960e6
#define SDRPLAY_L_MIN     1450e6
#define SDRPLAY_L_MAX     1675e6

#define SDRPLAY_MAX_BUF_SIZE 504

/*
 * Create a new instance of sdrplay_source_c and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
sdrplay_source_c_sptr
make_sdrplay_source_c (const std::string &args)
{
  return gnuradio::get_initial_sptr(new sdrplay_source_c (args));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr::block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept
 * only 0 input and 1 output.
 */
static const int MIN_IN = 0;	// mininum number of input streams
static const int MAX_IN = 0;	// maximum number of input streams
static const int MIN_OUT = 1;	// minimum number of output streams
static const int MAX_OUT = 1;	// maximum number of output streams

/*
 * The private constructor
 */
sdrplay_source_c::sdrplay_source_c (const std::string &args)
  : gr::sync_block ("sdrplay_source_c",
        gr::io_signature::make(MIN_IN, MAX_IN, sizeof (gr_complex)),
        gr::io_signature::make(MIN_OUT, MAX_OUT, sizeof (gr_complex))),
    _running(false),
    _uninit(false),
    _auto_gain(false)
{
   _dev = (sdrplay_dev_t *)malloc(sizeof(sdrplay_dev_t));
   if (_dev == NULL)
   {
      return;
}
   _dev->fsHz = 2048e3;
   _dev->rfHz = 200e6;
   _dev->bwType = mir_sdr_BW_1_536;
   _dev->ifType = mir_sdr_IF_Zero;
   _dev->samplesPerPacket = 0;
   _dev->dcMode = 0;
   _dev->gRdB = 60;
   set_gain_limits(_dev->rfHz);
   _dev->gain_dB = _dev->maxGain - _dev->gRdB;
   
   _bufi.reserve(SDRPLAY_MAX_BUF_SIZE);
   _bufq.reserve(SDRPLAY_MAX_BUF_SIZE);   

   _buf_mutex.lock();
   _buf_offset = 0;
   _buf_mutex.unlock();
}

/*
 * Our virtual destructor.
 */
sdrplay_source_c::~sdrplay_source_c ()
{
   if (_dev != NULL)
   {
      free(_dev);
      _dev = NULL;
   }
   _buf_mutex.lock();
   if (_running)
   {
      _running = false;
   }
   _uninit = true;
   _buf_mutex.unlock();
}

void sdrplay_source_c::reinit_device()
{
   std::cerr << "reinit_device started" << std::endl;
   _buf_mutex.lock();
   std::cerr << "after mutex.lock" << std::endl;
   if (_running)
   {
      std::cerr << "mir_sdr_Uninit started" << std::endl;
      mir_sdr_Uninit();
   }

   std::cerr << "mir_sdr_Init started" << std::endl;
   mir_sdr_Init(_dev->gRdB, _dev->fsHz / 1e6, _dev->rfHz / 1e6, _dev->bwType, _dev->ifType, &_dev->samplesPerPacket);

   if (_dev->dcMode)
   {
      std::cerr << "mir_sdr_SetDcMode started" << std::endl;
      mir_sdr_SetDcMode(4, 1);
   }

   _buf_offset = 0;
   _buf_mutex.unlock();
   std::cerr << "reinit_device end" << std::endl;
}

void sdrplay_source_c::set_gain_limits(double freq)
{
   if (freq <= SDRPLAY_AM_MAX)
   {
      _dev->minGain = -4;
      _dev->maxGain = 98;
   }
   else if (freq <= SDRPLAY_FM_MAX)
   {
      _dev->minGain = 1;
      _dev->maxGain = 103;
   }
   else if (freq <= SDRPLAY_B3_MAX)
   {
      _dev->minGain = 5;
      _dev->maxGain = 107;
   }
   else if (freq <= SDRPLAY_B45_MAX)
   {
      _dev->minGain = 9;
      _dev->maxGain = 94;
   }
   else if (freq <= SDRPLAY_L_MAX)
   {
      _dev->minGain = 24;
      _dev->maxGain = 105;
   }
}

int sdrplay_source_c::work( int noutput_items,
                            gr_vector_const_void_star &input_items,
                            gr_vector_void_star &output_items )
{
   gr_complex *out = (gr_complex *)output_items[0];
   int cnt = noutput_items;
   unsigned int sampNum;
   int grChanged;
   int rfChanged;
   int fsChanged;

   if (_uninit)
   {
      return WORK_DONE;
   }

   if (!_running)
   {
      reinit_device();
      _running = true;
   }

   _buf_mutex.lock();

   if (_buf_offset)
   {
      for (int i = _buf_offset; i < _dev->samplesPerPacket; i++)
      {
         *out++ = gr_complex( float(_bufi[i]) * (1.0f/2048.0f), float(_bufq[i]) * (1.0f/2048.0f) );
      }
      cnt -= (_dev->samplesPerPacket - _buf_offset);
   }

   while ((cnt - _dev->samplesPerPacket) >= 0)
   {
      mir_sdr_ReadPacket(_bufi.data(), _bufq.data(), &sampNum, &grChanged, &rfChanged, &fsChanged);
      for (int i = 0; i < _dev->samplesPerPacket; i++)
      {
         *out++ = gr_complex( float(_bufi[i]) * (1.0f/2048.0f), float(_bufq[i]) * (1.0f/2048.0f) );
      }
      cnt -= _dev->samplesPerPacket;
   }

   _buf_offset = 0;
   if (cnt)
   {
      mir_sdr_ReadPacket(_bufi.data(), _bufq.data(), &sampNum, &grChanged, &rfChanged, &fsChanged);
      for (int i = 0; i < cnt; i++)
      {
         *out++ = gr_complex( float(_bufi[i]) * (1.0f/2048.0f), float(_bufq[i]) * (1.0f/2048.0f) );
      }
      _buf_offset = cnt;
   }
   _buf_mutex.unlock();

   return noutput_items;
}

std::vector<std::string> sdrplay_source_c::get_devices()
{
   std::vector<std::string> devices;
   std::cerr << "get_devices started" << std::endl;

   unsigned int dev_cnt = 0;
   int samplesPerPacket;
   while(mir_sdr_Init(60, 2.048, 200.0, mir_sdr_BW_1_536, mir_sdr_IF_Zero, &samplesPerPacket) == mir_sdr_Success)
   {
      dev_cnt++;
   }

   std::cerr << "Device count: " << dev_cnt << std::endl;

   for (unsigned int i = 0; i < dev_cnt; i++) 
   {
      mir_sdr_Uninit();
      std::string args = "sdrplay=" + boost::lexical_cast< std::string >( i );
      args += ",label='" + std::string("SDRplay RSP") + "'";
      std::cerr << args << std::endl;
      devices.push_back( args );
   }

   std::cerr << "get_devices end" << std::endl;
   return devices;
}

size_t sdrplay_source_c::get_num_channels()
{
   std::cerr << "get_num_channels: 1" << std::endl;
   return 1;
}

osmosdr::meta_range_t sdrplay_source_c::get_sample_rates()
{
   osmosdr::meta_range_t range;

   range += osmosdr::range_t( 2000e3, 12000e3 ); 

   return range;
}

double sdrplay_source_c::set_sample_rate(double rate)
{
   std::cerr << "set_sample_rate start" << std::endl;
   double diff = rate - _dev->fsHz;
   _dev->fsHz = rate;

   std::cerr << "rate = " << rate << std::endl;
   std::cerr << "diff = " << diff << std::endl;
   if (_running) 
   {
      if (fabs(diff) < 10000.0)
      {
         std::cerr << "mir_sdr_SetFs started" << std::endl;
         mir_sdr_SetFs(diff, 0, 0, 0);
      }
      else
      {
         std::cerr << "reinit_device started" << std::endl;
         reinit_device();
      }
   }
   std::cerr << "set_sample_rate end" << std::endl;

   return get_sample_rate();
}

double sdrplay_source_c::get_sample_rate()
{
   if (_running)
   {
      return _dev->fsHz;
   }

//   return 0;
   return _dev->fsHz;
}

osmosdr::freq_range_t sdrplay_source_c::get_freq_range( size_t chan )
{
   osmosdr::freq_range_t range;

   range += osmosdr::range_t( SDRPLAY_AM_MIN,  SDRPLAY_AM_MAX ); /* LW/MW/SW (150 kHz - 30 MHz) */
   range += osmosdr::range_t( SDRPLAY_FM_MIN,  SDRPLAY_FM_MAX ); /* VHF Band II (64 - 108 MHz) */
   range += osmosdr::range_t( SDRPLAY_B3_MIN,  SDRPLAY_B3_MAX ); /* Band III (162 - 240 MHz) */
   range += osmosdr::range_t( SDRPLAY_B45_MIN, SDRPLAY_B45_MAX ); /* Band IV/V (470 - 960 MHz) */
   range += osmosdr::range_t( SDRPLAY_L_MIN,   SDRPLAY_L_MAX ); /* L-Band (1450 - 1675 MHz) */

   return range;
}

double sdrplay_source_c::set_center_freq( double freq, size_t chan )
{
   std::cerr << "set_center_freq start" << std::endl;
   std::cerr << "freq = " << freq << std::endl;
   double diff = freq - _dev->rfHz;
   std::cerr << "diff = " << diff << std::endl;
   _dev->rfHz = freq;
   set_gain_limits(freq);
   if (_running) 
   {
      if (fabs(diff) < 10000.0)
      {
         std::cerr << "mir_sdr_SetRf started" << std::endl;
         mir_sdr_SetRf(diff, 0, 0);
      }
      else
      {
         std::cerr << "reinit_device started" << std::endl;
         reinit_device();
      }
   }

   std::cerr << "set_center_freq end" << std::endl;
   return get_center_freq( chan );
}

double sdrplay_source_c::get_center_freq( size_t chan )
{
   if (_running)
   {
      return _dev->rfHz;
   }

//   return 0;
   return _dev->rfHz;
}

double sdrplay_source_c::set_freq_corr( double ppm, size_t chan )
{
   return get_freq_corr( chan );
}

double sdrplay_source_c::get_freq_corr( size_t chan )
{
   return 0;
}

std::vector<std::string> sdrplay_source_c::get_gain_names( size_t chan )
{
   std::vector< std::string > gains;

   gains += "LNA_MIX_BB";

   return gains;
}

osmosdr::gain_range_t sdrplay_source_c::get_gain_range( size_t chan )
{
   osmosdr::gain_range_t range;

   for (int i = _dev->minGain; i < _dev->maxGain; i++)
   {
      range += osmosdr::range_t( (float)i );
   }

   return range;
}

osmosdr::gain_range_t sdrplay_source_c::get_gain_range( const std::string & name, size_t chan )
{
   return get_gain_range( chan );
}

bool sdrplay_source_c::set_gain_mode( bool automatic, size_t chan )
{
   std::cerr << "set_gain_mode started" << std::endl;
   _auto_gain = automatic;
   std::cerr << "automatic = " << automatic << std::endl;
   if (automatic)
   {
      /* Start AGC */
      std::cerr << "AGC not yet implemented" << std::endl;
   }

   std::cerr << "set_gain_mode end" << std::endl;
   return get_gain_mode(chan);
}

bool sdrplay_source_c::get_gain_mode( size_t chan )
{
   return _auto_gain;
}

double sdrplay_source_c::set_gain( double gain, size_t chan )
{
   std::cerr << "set_gain started" << std::endl;
   _dev->gain_dB = gain;
   std::cerr << "gain = " << gain << std::endl;
   if (gain < _dev->minGain)
   {
      _dev->gain_dB = _dev->minGain;
   }
   if (gain > _dev->maxGain)
   {
      _dev->gain_dB = _dev->maxGain;
   }
   _dev->gRdB = (int)(_dev->maxGain - gain);

   if (_running) 
   {
      std::cerr << "mir_sdr_SetGr started" << std::endl;
      mir_sdr_SetGr(_dev->gRdB, 1, 0);
   }

std::cerr << "set_gain end" << std::endl;
return get_gain( chan );
}

double sdrplay_source_c::set_gain( double gain, const std::string & name, size_t chan)
{
   return set_gain( gain, chan );
}

double sdrplay_source_c::get_gain( size_t chan )
{
   if ( _running )
   {
      return _dev->gain_dB;
   }

//   return 0;
   return _dev->gain_dB;
}

double sdrplay_source_c::get_gain( const std::string & name, size_t chan )
{
   return get_gain( chan );
}

std::vector< std::string > sdrplay_source_c::get_antennas( size_t chan )
{
   std::vector< std::string > antennas;

   antennas += get_antenna( chan );

   return antennas;
}

std::string sdrplay_source_c::set_antenna( const std::string & antenna, size_t chan )
{
   return get_antenna( chan );
}

std::string sdrplay_source_c::get_antenna( size_t chan )
{
   return "RX";
}

void sdrplay_source_c::set_dc_offset_mode( int mode, size_t chan )
{
   if ( osmosdr::source::DCOffsetOff == mode ) 
   {
      _dev->dcMode = 0;
      if (_running)
      {
         mir_sdr_SetDcMode(4, 1);
      }
   }
   else if ( osmosdr::source::DCOffsetManual == mode ) 
   {
      std::cerr << "Manual DC correction mode is not implemented." << std::endl;
      _dev->dcMode = 0;
      if (_running)
      {
         mir_sdr_SetDcMode(4, 1);
      }
   }
   else if ( osmosdr::source::DCOffsetAutomatic == mode )
   {
      _dev->dcMode = 1;
      if (_running)
      {
         mir_sdr_SetDcMode(4, 1);
      }
   }
}

void sdrplay_source_c::set_dc_offset( const std::complex<double> &offset, size_t chan )
{
   std::cerr << "Manual DC correction mode is not implemented." << std::endl;
}

double sdrplay_source_c::set_bandwidth( double bandwidth, size_t chan )
{
   if      (bandwidth <= 200e3)  _dev->bwType = mir_sdr_BW_0_200;
   else if (bandwidth <= 300e3)  _dev->bwType = mir_sdr_BW_0_300;
   else if (bandwidth <= 600e3)  _dev->bwType = mir_sdr_BW_0_600;
   else if (bandwidth <= 1536e3) _dev->bwType = mir_sdr_BW_1_536;
   else if (bandwidth <= 5000e3) _dev->bwType = mir_sdr_BW_5_000;
   else if (bandwidth <= 6000e3) _dev->bwType = mir_sdr_BW_6_000;
   else if (bandwidth <= 7000e3) _dev->bwType = mir_sdr_BW_7_000;
   else                          _dev->bwType = mir_sdr_BW_8_000;

   if (_running) 
   {
      reinit_device();
   }

   return get_bandwidth( chan );
}

double sdrplay_source_c::get_bandwidth( size_t chan )
{
   double tmpbw=0.0f;
   if      (_dev->bwType == mir_sdr_BW_0_200) tmpbw =  200e3;
   else if (_dev->bwType == mir_sdr_BW_0_300) tmpbw =  300e3;
   else if (_dev->bwType == mir_sdr_BW_0_600) tmpbw =  600e3;
   else if (_dev->bwType == mir_sdr_BW_1_536) tmpbw = 1536e3;
   else if (_dev->bwType == mir_sdr_BW_5_000) tmpbw = 5000e3;
   else if (_dev->bwType == mir_sdr_BW_6_000) tmpbw = 6000e3;
   else if (_dev->bwType == mir_sdr_BW_7_000) tmpbw = 7000e3;
   else                                       tmpbw = 8000e3;
   
   return (double)tmpbw;
}

osmosdr::freq_range_t sdrplay_source_c::get_bandwidth_range( size_t chan )
{
   osmosdr::freq_range_t range;

   range += osmosdr::range_t( 200e3 ); 
   range += osmosdr::range_t( 300e3 ); 
   range += osmosdr::range_t( 600e3 ); 
   range += osmosdr::range_t( 1536e3 ); 
   range += osmosdr::range_t( 5000e3 ); 
   range += osmosdr::range_t( 6000e3 ); 
   range += osmosdr::range_t( 7000e3 ); 
   range += osmosdr::range_t( 8000e3 ); 

   return range;
}
