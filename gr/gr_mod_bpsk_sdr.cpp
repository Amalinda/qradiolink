// Written by Adrian Musceac YO8RZZ , started March 2016.
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

#include "gr_mod_bpsk_sdr.h"


gr_mod_bpsk_sdr_sptr make_gr_mod_bpsk_sdr(int sps, int samp_rate, int carrier_freq,
                                          int filter_width)
{
    return gnuradio::get_initial_sptr(new gr_mod_bpsk_sdr(sps, samp_rate, carrier_freq,
                                                      filter_width));
}

gr_mod_bpsk_sdr::gr_mod_bpsk_sdr(int sps, int samp_rate, int carrier_freq,
                                 int filter_width) :
    gr::hier_block2 ("gr_mod_bpsk_sdr",
                      gr::io_signature::make (1, 1, sizeof (char)),
                      gr::io_signature::make (1, 1, sizeof (gr_complex)))
{
    std::vector<gr_complex> constellation;
    constellation.push_back(-1);
    constellation.push_back(1);
    std::vector<int> polys;
    polys.push_back(109);
    polys.push_back(79);

    _samples_per_symbol = sps;
    _samp_rate =samp_rate;
    _carrier_freq = carrier_freq;
    _filter_width = filter_width;

    _packed_to_unpacked = gr::blocks::packed_to_unpacked_bb::make(1,gr::GR_MSB_FIRST);
    _unpacked_to_packed = gr::blocks::unpacked_to_packed_bb::make(1,gr::GR_MSB_FIRST);
    _scrambler = gr::digital::scrambler_bb::make(0x8A, 0x7F ,7);

    _ccsds_encoder = gr::fec::encode_ccsds_27_bb::make();

    _chunks_to_symbols = gr::digital::chunks_to_symbols_bc::make(constellation);
    int nfilts = 32;
    std::vector<float> rrc_taps = gr::filter::firdes::root_raised_cosine(nfilts, nfilts,
                                                        1, 0.35, nfilts * 11 * _samples_per_symbol);
    _shaping_filter = gr::filter::pfb_arb_resampler_ccf::make(_samples_per_symbol, rrc_taps, nfilts);
    _repeat = gr::blocks::repeat::make(8, _samples_per_symbol);
    _amplify = gr::blocks::multiply_const_cc::make(0.3,1);
    _filter = gr::filter::fft_filter_ccf::make(
                1,gr::filter::firdes::low_pass(
                    1, _samp_rate, _filter_width, 600, gr::filter::firdes::WIN_HAMMING));


    connect(self(),0,_packed_to_unpacked,0);
    connect(_packed_to_unpacked,0,_scrambler,0);
    connect(_scrambler,0,_unpacked_to_packed,0);
    connect(_unpacked_to_packed,0,_ccsds_encoder,0);
    connect(_ccsds_encoder,0,_chunks_to_symbols,0);
    connect(_chunks_to_symbols,0,_repeat,0);

    connect(_repeat,0,_amplify,0);
    connect(_amplify,0,_filter,0);

    connect(_filter,0,self(),0);

}




