// Written by Adrian Musceac YO8RZZ at gmail dot com, started March 2016.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "gr_demod_qpsk_sdr.h"

gr_demod_qpsk_sdr::gr_demod_qpsk_sdr(gr::qtgui::sink_c::sptr fft_gui, gr::qtgui::const_sink_c::sptr const_gui,
                                     gr::qtgui::number_sink::sptr rssi_gui, QObject *parent,
                                     int sps, int samp_rate, int target_samp_rate, int carrier_freq,
                                     int filter_width, float mod_index, float device_frequency, float rf_gain,
                                     std::string device_args, std::string device_antenna, int freq_corr) :
    QObject(parent)
{
    _target_samp_rate = target_samp_rate;
    _rssi = rssi_gui;
    _device_frequency = device_frequency;
    int decimation;
    int interpolation;
    if(_target_samp_rate == 20000)
    {
        interpolation = 1;
        decimation = 50;
        _samples_per_symbol = sps*2/25;
    }
    else
    {
        interpolation = 1;
        decimation = 4;
        _samples_per_symbol = sps;
    }
    _samp_rate =samp_rate;
    _carrier_freq = carrier_freq;
    _filter_width = filter_width;
    int filter_slope = 600;
    if(_target_samp_rate > 100000)
        filter_slope = 10000;

    _modulation_index = mod_index;
    _top_block = gr::make_top_block("qpsk demodulator sdr");

    std::vector<int> map;
    map.push_back(0);
    map.push_back(1);
    map.push_back(3);
    map.push_back(2);

    gr::digital::constellation_dqpsk::sptr constellation = gr::digital::constellation_dqpsk::make();

    float rerate = (float)_target_samp_rate/(float)_samp_rate;

    unsigned int flt_size = 32;
    /*
    gr::digital::constellation_expl_rect::sptr constellation = gr::digital::constellation_expl_rect::make(
                constellation->points(),pre_diff_code,4,2,2,1,1,const_map);
    */

    std::vector<float> taps = gr::filter::firdes::low_pass(flt_size, _samp_rate, _filter_width, 12000);

    _resampler = gr::filter::rational_resampler_base_ccf::make(interpolation, decimation, taps);

    //_resampler_pfb = gr::filter::pfb_arb_resampler_ccf::make(rerate, taps, flt_size);

    _agc = gr::analog::agc2_cc::make(0.06e-1, 1e-3, 1, 1);
    _signal_source = gr::analog::sig_source_c::make(_samp_rate,gr::analog::GR_COS_WAVE,-250000,1);
    _multiply = gr::blocks::multiply_cc::make();
    /*
    _freq_transl_filter = gr::filter::freq_xlating_fir_filter_ccf::make(
                1,gr::filter::firdes::low_pass(
                    1, _target_samp_rate, 2*_filter_width, 250000, gr::filter::firdes::WIN_HAMMING), 25000,
                _target_samp_rate);
    */
    _filter = gr::filter::fft_filter_ccf::make(1, gr::filter::firdes::low_pass(
                                1, _target_samp_rate, _filter_width, filter_slope,gr::filter::firdes::WIN_HAMMING) );
    float gain_mu, omega_rel_limit;
    if(_target_samp_rate == 20000)
    {
        gain_mu = 0.05;
        omega_rel_limit = 0.025;
    }
    else
    {
        gain_mu = 0.0025;
        omega_rel_limit = 0.0015;
    }
    _clock_recovery = gr::digital::clock_recovery_mm_cc::make(_samples_per_symbol, 0.025*gain_mu*gain_mu, 0.5, gain_mu,
                                                              omega_rel_limit);
    std::vector<float> pfb_taps = gr::filter::firdes::root_raised_cosine(flt_size,flt_size, 1, 0.35, flt_size * 11 * _samples_per_symbol);
    _clock_sync = gr::digital::pfb_clock_sync_ccf::make(_samples_per_symbol,0.0628,pfb_taps);
    _costas_loop = gr::digital::costas_loop_cc::make(0.0628,4);
    _equalizer = gr::digital::cma_equalizer_cc::make(8,4,0.00005,1);
    _fll = gr::digital::fll_band_edge_cc::make(sps, 0.55, 32, 0.000628);
    _diff_decoder = gr::digital::diff_decoder_bb::make(4);
    _map = gr::digital::map_bb::make(map);
    _unpack = gr::blocks::unpack_k_bits_bb::make(2);
    _descrambler = gr::digital::descrambler_bb::make(0x8A, 0x7F ,7);
    _constellation_receiver = gr::digital::constellation_decoder_cb::make(constellation);
    _vector_sink = make_gr_vector_sink();

    _rssi_valve = gr::blocks::copy::make(8);
    _rssi_valve->set_enabled(false);
    _fft_valve = gr::blocks::copy::make(8);
    _fft_valve->set_enabled(false);
    _const_valve = gr::blocks::copy::make(8);
    _const_valve->set_enabled(false);
    _mag_squared = gr::blocks::complex_to_mag_squared::make();
    _single_pole_filter = gr::filter::single_pole_iir_filter_ff::make(0.04);
    _log10 = gr::blocks::nlog10_ff::make();
    _multiply_const_ff = gr::blocks::multiply_const_ff::make(10);
    _moving_average = gr::blocks::moving_average_ff::make(25000,1,2000);
    _add_const = gr::blocks::add_const_ff::make(-110);

    _osmosdr_source = osmosdr::source::make(device_args);
    _osmosdr_source->set_center_freq(_device_frequency-250000);
    _osmosdr_source->set_sample_rate(_samp_rate);
    _osmosdr_source->set_freq_corr(freq_corr);
    _osmosdr_source->set_gain_mode(false);
    _osmosdr_source->set_dc_offset_mode(0);
    _osmosdr_source->set_iq_balance_mode(0);
    _osmosdr_source->set_antenna(device_antenna);
    osmosdr::gain_range_t range = _osmosdr_source->get_gain_range();
    if (!range.empty())
    {
        double gain =  range.start() + rf_gain*(range.stop()-range.start());
        _osmosdr_source->set_gain(gain);
    }
    else
    {
        _osmosdr_source->set_gain_mode(true);
    }
    _constellation = const_gui;
    _fft_gui = fft_gui;
    _top_block->connect(_osmosdr_source,0,_multiply,0);
    _top_block->connect(_signal_source,0,_multiply,1);
    _top_block->connect(_multiply,0,_fft_valve,0);
    _top_block->connect(_fft_valve,0,_fft_gui,0);
    _top_block->connect(_multiply,0,_resampler,0);
    _top_block->connect(_resampler,0,_filter,0);

    _top_block->connect(_filter,0,_agc,0);
    _top_block->connect(_agc,0,_clock_recovery,0);
    //_top_block->connect(_fll,0,_clock_recovery,0);
    _top_block->connect(_clock_recovery,0,_costas_loop,0);
    //_top_block->connect(_equalizer,0,_costas_loop,0);
    _top_block->connect(_costas_loop,0,_const_valve,0);
    _top_block->connect(_const_valve,0,_constellation,0);
    _top_block->connect(_costas_loop,0,_constellation_receiver,0);
    _top_block->connect(_constellation_receiver,0,_diff_decoder,0);
    _top_block->connect(_diff_decoder,0,_map,0);
    _top_block->connect(_map,0,_unpack,0);
    _top_block->connect(_unpack,0,_descrambler,0);
    _top_block->connect(_descrambler,0,_vector_sink,0);

    _top_block->connect(_filter,0,_rssi_valve,0);
    _top_block->connect(_rssi_valve,0,_mag_squared,0);
    _top_block->connect(_mag_squared,0,_moving_average,0);
    _top_block->connect(_moving_average,0,_single_pole_filter,0);
    _top_block->connect(_single_pole_filter,0,_log10,0);
    _top_block->connect(_log10,0,_multiply_const_ff,0);
    _top_block->connect(_multiply_const_ff,0,_add_const,0);
    _top_block->connect(_add_const,0,_rssi,0);
}

void gr_demod_qpsk_sdr::start()
{
    _top_block->start();
}

void gr_demod_qpsk_sdr::stop()
{
    _top_block->stop();
    _top_block->wait();
}

std::vector<unsigned char>* gr_demod_qpsk_sdr::getData()
{
    std::vector<unsigned char> *data = _vector_sink->get_data();
    return data;
}

void gr_demod_qpsk_sdr::tune(long center_freq)
{
    _device_frequency = center_freq;
    _osmosdr_source->set_center_freq(_device_frequency - 250000.0);
}

void gr_demod_qpsk_sdr::set_rx_sensitivity(float value)
{
    osmosdr::gain_range_t range = _osmosdr_source->get_gain_range();
    if (!range.empty())
    {
        double gain =  range.start() + value*(range.stop()-range.start());
        _osmosdr_source->set_gain(gain);
    }
}

void gr_demod_qpsk_sdr::enable_gui(bool value)
{
    _rssi_valve->set_enabled(value);
    _fft_valve->set_enabled(value);
    _const_valve->set_enabled(value);
}
