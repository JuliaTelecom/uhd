//
// Copyright 2017 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0
//

#include "magnesium_radio_ctrl_impl.hpp"
#include "spi_core_3000.hpp"
#include <uhd/utils/log.hpp>
#include <uhd/rfnoc/node_ctrl_base.hpp>
#include <uhd/transport/chdr.hpp>
#include <uhd/utils/math.hpp>
#include <uhd/types/direction.hpp>
#include <uhd/types/eeprom.hpp>
#include <uhd/exception.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <sstream>

using namespace uhd;
using namespace uhd::usrp;
using namespace uhd::rfnoc;

namespace {
    enum slave_select_t {
        SEN_CPLD = 1,
        SEN_TX_LO = 2,
        SEN_RX_LO = 4,
        SEN_PHASE_DAC = 8
    };

    const double MAGNESIUM_TICK_RATE = 125e6; // Hz
    const double MAGNESIUM_RADIO_RATE = 125e6; // Hz
    const double MAGNESIUM_MIN_FREQ = 1e6; // Hz
    const double MAGNESIUM_MAX_FREQ = 6e9; // Hz
    const double MAGNESIUM_MIN_RX_GAIN = 0.0; // dB
    const double MAGNESIUM_MAX_RX_GAIN = 30.0; // dB
    const double MAGNESIUM_RX_GAIN_STEP = 0.5;
    const double MAGNESIUM_MIN_TX_GAIN = 0.0; // dB
    const double MAGNESIUM_MAX_TX_GAIN = 41.95; // dB
    const double MAGNESIUM_TX_GAIN_STEP = 0.05;
    const double MAGNESIUM_CENTER_FREQ = 2.5e9; // Hz
    const char* MAGNESIUM_DEFAULT_RX_ANTENNA = "RX2";
    const char* MAGNESIUM_DEFAULT_TX_ANTENNA = "TX/RX";
    const double MAGNESIUM_DEFAULT_GAIN = 0.0; // dB
    const double MAGNESIUM_DEFAULT_BANDWIDTH = 40e6; // Hz TODO: fix
    const size_t MAGNESIUM_NUM_TX_CHANS = 1;
    const size_t MAGNESIUM_NUM_RX_CHANS = 1;
    const double MAGNESIUM_RX_IF_FREQ = 2.44e9;
    const double MAGNESIUM_TX_IF_FREQ = 1.95e9;
    const double MAGNESIUM_LOWBAND_FREQ = 300e6;
/*
Magnesium Rev C frequency bands:

RX IF frequency is 2.4418 GHz. Have 80 MHz of bandwidth for loband.
TX IF frequency is 1.8-2.1 GHz (1.95 GHz is best).

For RX:
	Band   	  SW2-AB SW3-ABC SW4-ABC SW5-ABCD SW6-ABC SW7-AB SW8-AB MIX
	WB     	  RF1 01 OFF 111 NA  --- NA  ---- RF3 001 RF2 01 RF2 01 0
	LB     	  RF2 10 RF5 100 NA  --- RF3 0010 RF1 100 RF1 10 RF1 10 1
	440-530	  RF2 10 RF2 001 NA  --- RF1 1000 RF1 100 RF2 01 RF2 01 0
	650-1000  RF2 10 RF6 101 NA  --- RF4 0001 RF1 100 RF2 01 RF2 01 0
	1100-1575 RF2 10 RF4 011 NA  --- RF2 0100 RF1 100 RF2 01 RF2 01 0
	1600-2250 RF2 10 RF3 010 RF2 010 NA  ---- RF2 010 RF2 01 RF2 01 0
	2100-2850 RF2 10 RF1 000 RF1 100 NA  ---- RF2 010 RF2 01 RF2 01 0
	2700+     RF3 11 OFF 111 RF3 001 NA  ---- RF2 010 RF2 01 RF2 01 0

For TX:
	Band      SW5-AB SW4-AB SW3-X SW2-ABCD SW1-AB SWTRX-AB MIX
	WB        RF1 10 RF2 01 RF1 0 NA  ---- SHD 00 RF4   11 0
	LB        RF2 01 RF1 10 RF2 1 RF3 0010 RF3 11 RF1   00 1
	<800      RF1 10 RF2 01 RF2 1 RF3 0010 RF3 11 RF1   00 0
	800-1700  RF1 10 RF2 01 RF2 1 RF2 0100 RF2 10 RF1   00 0
	1700-3400 RF1 10 RF2 01 RF2 1 RF1 1000 RF1 01 RF1   00 0
	3400-6400 RF1 10 RF2 01 RF2 1 RF4 0001 SHD 00 RF2   10 0

*/


    const double MAGNESIUM_RX_BAND1_MIN_FREQ = MAGNESIUM_LOWBAND_FREQ;
    const double MAGNESIUM_RX_BAND2_MIN_FREQ = 600e6;
    const double MAGNESIUM_RX_BAND3_MIN_FREQ = 1050e6;
    const double MAGNESIUM_RX_BAND4_MIN_FREQ = 1600e6;
    const double MAGNESIUM_RX_BAND5_MIN_FREQ = 2100e6;
    const double MAGNESIUM_RX_BAND6_MIN_FREQ = 2700e6;

    const double MAGNESIUM_TX_BAND1_MIN_FREQ = MAGNESIUM_LOWBAND_FREQ;
    const double MAGNESIUM_TX_BAND2_MIN_FREQ = 800e6;
    const double MAGNESIUM_TX_BAND3_MIN_FREQ = 1700e6;
    const double MAGNESIUM_TX_BAND4_MIN_FREQ = 3400e6;

    const size_t FPGPIO_MASTER_RADIO = 0;

    /*! Return a valid 'which' string for use with AD9371 API calls
     *
     * These strings take the form of "RX1", "TX2", ...
     */
    std::string _get_which(const direction_t dir, const size_t chan)
    {
        UHD_ASSERT_THROW(dir == RX_DIRECTION or dir == TX_DIRECTION);
        UHD_ASSERT_THROW(chan == 0 or chan == 1);
        size_t chan = 0;
        if (_radio_slot == "A" or _radio_slot == "C")
        {
             chan = 0;
        }
        if (_radio_slot == "B" or _radio_slot == "D")
        {
            chan = 1;
        }
        UHD_LOG_WARNING("MAGNESIUM_MYKONOS","board slot to chan map " << _radio_slot << " "<<chan)
        return str(boost::format("%s%d")
                   % (dir == RX_DIRECTION ? "RX" : "TX")
                   % (chan+1)
        );
    }
}


/******************************************************************************
 * Structors
 *****************************************************************************/
UHD_RFNOC_RADIO_BLOCK_CONSTRUCTOR(magnesium_radio_ctrl)
{
    UHD_LOG_TRACE("MAGNESIUM", "Entering magnesium_radio_ctrl_impl ctor...");
    UHD_LOG_DEBUG("MAGNESIUM", "Note: Running in one-block-per-channel mode!");
    const char radio_slot_name[4] = {'A','B','C','D'};
    _radio_slot = radio_slot_name[get_block_id().get_block_count()];
    UHD_LOG_TRACE("MAGNESIUM", "Radio slot: " << _radio_slot);
    _rpc_prefix =
        (get_block_id().get_block_count() % 2 == 0) ? "db_0_" : "db_1_";
    UHD_LOG_TRACE("MAGNESIUM", "Using RPC prefix `" << _rpc_prefix << "'");

    _init_peripherals();
    _init_defaults();

    //////// REST OF CTOR IS PROP TREE SETUP //////////////////////////////////

    /**** Set up legacy compatible properties ******************************/
    // For use with multi_usrp APIs etc.
    // For legacy prop tree init:
    // TODO: determine DB number
    const fs_path fe_base = fs_path("dboards") / _radio_slot;
    const std::vector<uhd::direction_t> dir({ RX_DIRECTION, TX_DIRECTION });
    const std::vector<std::string> fe({ "rx_frontends", "tx_frontends" });
    const std::vector<std::string> ant({ "RX" , "TX" });
    const std::vector<size_t> num_chans({ MAGNESIUM_NUM_RX_CHANS , MAGNESIUM_NUM_TX_CHANS });
    const size_t RX_IDX = 0;
    // const size_t TX_IDX = 1;

    for (size_t fe_idx = 0; fe_idx < fe.size(); ++fe_idx)
    {
        const fs_path fe_direction_path = fe_base / fe[fe_idx];
        for (size_t chan = 0; chan < num_chans[fe_idx]; ++chan)
        {
            const fs_path fe_path = fe_direction_path / chan;
            UHD_LOG_TRACE("MAGNESIUM", "Adding FE at " << fe_path);
            // Shared TX/RX attributes
            _tree->create<std::string>(fe_path / "name")
                .set(str(boost::format("Magnesium %s %d") % ant[fe_idx] % chan))
                ;
            _tree->create<std::string>(fe_path / "connection")
                .set("IQ")
                ;
            {
                // TODO: fix antenna name
                // Now witness the firepower of this fully armed and operational lambda
                auto dir_ = dir[fe_idx];
                auto coerced_lambda = [this, chan, dir_](const std::string &ant)
                {
                    return this->_myk_set_antenna(ant, chan, dir_);
                };
                auto publisher_lambda = [this, chan, dir_]()
                {
                    return this->_myk_get_antenna(chan, dir_);
                };
                _tree->create<std::string>(fe_path / "antenna" / "value")
                    .set(str(boost::format("%s%d") % ant[fe_idx] % (chan + 1)))
                    .add_coerced_subscriber(coerced_lambda)
                    .set_publisher(publisher_lambda);
                // TODO: fix options
                _tree->create<std::vector<std::string>>(fe_path / "antenna" / "options")
                    .set(std::vector<std::string>(1, str(boost::format("%s%d") % ant[fe_idx] % (chan + 1))));
            }
            {
                auto dir_ = dir[fe_idx];
                auto coerced_lambda = [this, chan, dir_](const double freq)
                {
                    return this->_myk_set_frequency(freq, chan, dir_);
                };
                auto publisher_lambda = [this, chan, dir_]()
                {
                    return this->_myk_get_frequency(chan, dir_);
                };
                _tree->create<double>(fe_path / "freq" / "value")
                    .set(MAGNESIUM_CENTER_FREQ)
                    .set_coercer(coerced_lambda)
                    .set_publisher(publisher_lambda);
                _tree->create<meta_range_t>(fe_path / "freq" / "range")
                    .set(meta_range_t(MAGNESIUM_MIN_FREQ, MAGNESIUM_MAX_FREQ));
            }
            {
                auto dir_ = dir[fe_idx];
                auto coerced_lambda = [this, chan, dir_](const double gain)
                {
                    return this->_myk_set_gain(gain, chan, dir_);
                };
                auto publisher_lambda = [this, chan, dir_]()
                {
                    return this->_myk_get_gain(chan, dir_);
                };
                auto min_gain = (fe_idx == RX_IDX) ? MAGNESIUM_MIN_RX_GAIN : MAGNESIUM_MIN_TX_GAIN;
                auto max_gain = (fe_idx == RX_IDX) ? MAGNESIUM_MAX_RX_GAIN : MAGNESIUM_MAX_TX_GAIN;
                auto gain_step = (fe_idx == RX_IDX) ? MAGNESIUM_RX_GAIN_STEP : MAGNESIUM_TX_GAIN_STEP;
                // TODO: change from null
                _tree->create<double>(fe_path / "gains" / "null" / "value")
                    .set(MAGNESIUM_DEFAULT_GAIN)
                    .set_coercer(coerced_lambda)
                    .set_publisher(publisher_lambda);
                _tree->create<meta_range_t>(fe_path / "gains" / "null" / "range")
                    .set(meta_range_t(min_gain, max_gain, gain_step));
            }
            // TODO: set up read/write of bandwidth properties correctly
            if (fe_idx == RX_IDX)
            {
                auto coerced_lambda = [this, chan](const double bw)
                {
                    return this->set_rx_bandwidth(bw, chan);
                };
                auto publisher_lambda = [this, chan]()
                {
                    return this->get_rx_bandwidth(chan);
                };
                _tree->create<double>(fe_path / "bandwidth" / "value")
                    .set(MAGNESIUM_DEFAULT_BANDWIDTH)
                    .set_coercer(coerced_lambda)
                    .set_publisher(publisher_lambda);
            }
            else {
                _tree->create<double>(fe_path / "bandwidth" / "value")
                    .set(MAGNESIUM_DEFAULT_BANDWIDTH);
            }
            _tree->create<meta_range_t>(fe_path / "bandwidth" / "range")
                .set(meta_range_t(MAGNESIUM_DEFAULT_BANDWIDTH, MAGNESIUM_DEFAULT_BANDWIDTH));
        }
    }

    // EEPROM paths subject to change FIXME
    _tree->create<eeprom_map_t>(_root_path / "eeprom").set(eeprom_map_t());

    // TODO change codec names
    _tree->create<int>("rx_codecs" / _radio_slot / "gains");
    _tree->create<int>("tx_codecs" / _radio_slot / "gains");
    _tree->create<std::string>("rx_codecs" / _radio_slot / "name").set("AD9361 Dual ADC");
    _tree->create<std::string>("tx_codecs" / _radio_slot / "name").set("AD9361 Dual DAC");

    // TODO remove this dirty hack
    if (not _tree->exists("tick_rate"))
    {
        _tree->create<double>("tick_rate").set(MAGNESIUM_TICK_RATE);
    }
}

magnesium_radio_ctrl_impl::~magnesium_radio_ctrl_impl()
{
    UHD_LOG_TRACE("MAGNESIUM", "magnesium_radio_ctrl_impl::dtor() ");
}

/**************************************************************************
 * Init Helpers
 *************************************************************************/
void magnesium_radio_ctrl_impl::_init_peripherals()
{
    UHD_LOG_TRACE("MAGNESIUM", "Initializing peripherals...");
    fs_path cpld_path  = _root_path.branch_path()
        / str(boost::format("Radio_%d") % ((get_block_id().get_block_count()/2)*2))
        / "cpld";
    fs_path rx_lo_path  = _root_path.branch_path()
        / str(boost::format("Radio_%d") % ((get_block_id().get_block_count()/2)*2))
        / "rx_lo";
    fs_path tx_lo_path  = _root_path.branch_path()
        / str(boost::format("Radio_%d") % ((get_block_id().get_block_count()/2)*2))
        / "tx_lo";
    // TODO: When we move back to 2 chans per RFNoC block, this needs to be
    // non-conditional, and the else-branch goes away:
    if (_radio_slot == "A" or _radio_slot == "C") {
        UHD_LOG_TRACE("MAGNESIUM", "Initializing SPI core...");
        _spi = spi_core_3000::make(_get_ctrl(0),
            radio_ctrl_impl::regs::sr_addr(radio_ctrl_impl::regs::SPI),
            radio_ctrl_impl::regs::RB_SPI);
    } else {
        UHD_LOG_TRACE("MAGNESIUM", "Not a master radio, no SPI core.");
    }

    UHD_LOG_TRACE("MAGNESIUM", "Initializing CPLD...");
    UHD_LOG_TRACE("MAGNESIUM", "CPLD path: " << cpld_path);
    if (not _tree->exists(cpld_path)) {
        UHD_LOG_TRACE("MAGNESIUM", "Creating new CPLD object...");
        spi_config_t spi_config;
        spi_config.use_custom_divider = true;
        spi_config.divider = 125;
        spi_config.mosi_edge = spi_config_t::EDGE_RISE;
        spi_config.miso_edge = spi_config_t::EDGE_FALL;
        UHD_LOG_TRACE("MAGNESIUM", "Making CPLD object...");
        _cpld = std::make_shared<magnesium_cpld_ctrl>(
            [this, spi_config](const uint32_t transaction){ // Write functor
                this->_spi->write_spi(
                    SEN_CPLD,
                    spi_config,
                    transaction,
                    24
                );
            },
            [this, spi_config](const uint32_t transaction){ // Read functor
                return this->_spi->read_spi(
                    SEN_CPLD,
                    spi_config,
                    transaction,
                    24
                );
            }
        );
        _tree->create<magnesium_cpld_ctrl::sptr>(cpld_path).set(_cpld);
    } else {
        UHD_LOG_TRACE("MAGNESIUM", "Reusing someone else's CPLD object...");
        _cpld = _tree->access<magnesium_cpld_ctrl::sptr>(cpld_path).get();
    }

    // TODO: Same comment as above applies
    if (_radio_slot == "A" or _radio_slot == "C") {
        UHD_LOG_TRACE("MAGNESIUM", "Initializing TX LO...");
        _tx_lo = adf435x_iface::make_adf4351(
            [this](const std::vector<uint32_t> transactions){
                for (const uint32_t transaction: transactions) {
                    this->_spi->write_spi(
                        SEN_TX_LO,
                        spi_config_t::EDGE_RISE,
                        transaction,
                        32
                    );
                }
            }
        );
        UHD_LOG_TRACE("MAGNESIUM", "Initializing RX LO...");
        _rx_lo = adf435x_iface::make_adf4351(
            [this](const std::vector<uint32_t> transactions){
                for (const uint32_t transaction: transactions) {
                    this->_spi->write_spi(
                        SEN_RX_LO,
                        spi_config_t::EDGE_RISE,
                        transaction,
                        32
                    );
                }
            }
        );
    } else {
        UHD_LOG_TRACE("MAGNESIUM", "Not a master radio, no LOs.");
    }
    if (not _tree->exists(rx_lo_path)) {
        _tree->create<adf435x_iface::sptr>(rx_lo_path).set(_rx_lo);
    }else
    {
        UHD_LOG_TRACE("MAGNESIUM", "Not a master radio. Getting LO from master" );
        _rx_lo = _tree->access<adf435x_iface::sptr>(rx_lo_path).get();
    }
    if (not _tree->exists(tx_lo_path)) {
        _tree->create<adf435x_iface::sptr>(tx_lo_path).set(_tx_lo);
    }else
    {
        UHD_LOG_TRACE("MAGNESIUM", "Not a master radio. Getting LO from master" );
        _tx_lo = _tree->access<adf435x_iface::sptr>(tx_lo_path).get();
    }

    _gpio.clear(); // Following the as-if rule, this can get optimized out
    for (size_t radio_idx = 0; radio_idx < _get_num_radios(); radio_idx++) {
        UHD_LOG_TRACE("MAGNESIUM",
            "Initializing GPIOs for channel " << radio_idx);
        _gpio.emplace_back(
            gpio_atr::gpio_atr_3000::make(
                _get_ctrl(radio_idx),
                regs::sr_addr(regs::ATR)
            )
        );
        // DSA and AD9371 gain bits do *not* toggle on ATR modes. If we ever
        // connect anything else to this core, we might need to set_atr_mode()
        // to MODE_ATR on those bits. For now, all bits simply do what they're
        // told, and don't toggle on RX/TX state changes.
         _gpio.back()->set_atr_mode(
             usrp::gpio_atr::MODE_GPIO, // Disable ATR mode
             usrp::gpio_atr::gpio_atr_3000::MASK_SET_ALL
         );
         _gpio.back()->set_gpio_ddr(
            usrp::gpio_atr::DDR_OUTPUT, // Make all GPIOs outputs
            usrp::gpio_atr::gpio_atr_3000::MASK_SET_ALL
        );
    }

    if (get_block_id().get_block_count() == FPGPIO_MASTER_RADIO) {
        UHD_LOG_TRACE(unique_id(), "Initializing front-panel GPIO control...")
        _fp_gpio = gpio_atr::gpio_atr_3000::make(
                _get_ctrl(0), regs::sr_addr(regs::FP_GPIO), regs::RB_FP_GPIO);
    }
}

void magnesium_radio_ctrl_impl::_init_defaults()
{
    UHD_LOG_TRACE("MAGNESIUM", "Initializing defaults...");
    const size_t num_rx_chans = get_output_ports().size();
    //UHD_ASSERT_THROW(num_rx_chans == MAGNESIUM_NUM_RX_CHANS);
    const size_t num_tx_chans = get_input_ports().size();
    //UHD_ASSERT_THROW(num_tx_chans == MAGNESIUM_NUM_TX_CHANS);

    UHD_LOG_TRACE("MAGNESIUM",
            "Num TX chans: " << num_tx_chans
            << " Num RX chans: " << num_rx_chans);
    UHD_LOG_TRACE("MAGNESIUM",
            "Setting tick rate to " << MAGNESIUM_TICK_RATE / 1e6 << " MHz");
    radio_ctrl_impl::set_rate(MAGNESIUM_TICK_RATE);

    for (size_t chan = 0; chan < num_rx_chans; chan++) {
        radio_ctrl_impl::set_rx_frequency(MAGNESIUM_CENTER_FREQ, chan);
        radio_ctrl_impl::set_rx_gain(MAGNESIUM_DEFAULT_GAIN, chan);
        radio_ctrl_impl::set_rx_antenna(MAGNESIUM_DEFAULT_RX_ANTENNA, chan);
        radio_ctrl_impl::set_rx_bandwidth(MAGNESIUM_DEFAULT_BANDWIDTH, chan);
    }

    for (size_t chan = 0; chan < num_tx_chans; chan++) {
        radio_ctrl_impl::set_tx_frequency(MAGNESIUM_CENTER_FREQ, chan);
        radio_ctrl_impl::set_tx_gain(MAGNESIUM_DEFAULT_GAIN, chan);
        radio_ctrl_impl::set_tx_antenna(MAGNESIUM_DEFAULT_TX_ANTENNA, chan);
    }
}


/******************************************************************************
 * API Calls
 *****************************************************************************/
double magnesium_radio_ctrl_impl::set_rate(double rate)
{
    // TODO: implement
    if (rate != get_rate()) {
        UHD_LOG_WARNING("MAGNESIUM",
                "Attempting to set sampling rate to invalid value " << rate);
    }
    return get_rate();
}

void magnesium_radio_ctrl_impl::set_tx_antenna(
        const std::string &ant,
        const size_t chan
) {
    _myk_set_antenna(ant, chan, TX_DIRECTION);
}

void magnesium_radio_ctrl_impl::set_rx_antenna(
        const std::string &ant,
        const size_t chan
) {
    _myk_set_antenna(ant, chan, RX_DIRECTION);
}

double magnesium_radio_ctrl_impl::set_tx_frequency(
        const double freq,
        const size_t chan
) {
    return _myk_set_frequency(freq, chan, TX_DIRECTION);
}

double magnesium_radio_ctrl_impl::set_rx_frequency(
        const double freq,
        const size_t chan
) {
    return _myk_set_frequency(freq, chan, RX_DIRECTION);
}

double magnesium_radio_ctrl_impl::set_rx_bandwidth(
        const double bandwidth,
        const size_t chan
) {
    return _myk_set_bandwidth(bandwidth, chan, RX_DIRECTION);
}

double magnesium_radio_ctrl_impl::set_tx_gain(
        const double gain,
        const size_t chan
) {
    return _myk_set_gain(gain, chan, TX_DIRECTION);
}

double magnesium_radio_ctrl_impl::set_rx_gain(
        const double gain,
        const size_t chan
) {
    return _myk_set_gain(gain, chan, RX_DIRECTION);
}

std::string magnesium_radio_ctrl_impl::get_tx_antenna(
        const size_t chan
) /* const */ {
    return _myk_get_antenna(chan, TX_DIRECTION);
}

std::string magnesium_radio_ctrl_impl::get_rx_antenna(
        const size_t chan
) /* const */ {
    return _myk_get_antenna(chan, RX_DIRECTION);
}

double magnesium_radio_ctrl_impl::get_tx_frequency(
        const size_t chan
) /* const */ {
    return _myk_get_frequency(chan, TX_DIRECTION);
}

double magnesium_radio_ctrl_impl::get_rx_frequency(
        const size_t chan
) /* const */ {
    return _myk_get_frequency(chan, RX_DIRECTION);
}

double magnesium_radio_ctrl_impl::get_tx_gain(
    const size_t chan
) /* const */ {
    return _myk_get_gain(chan, TX_DIRECTION);
}

double magnesium_radio_ctrl_impl::get_rx_gain(
    const size_t chan
) /* const */ {
    return _myk_get_gain(chan, RX_DIRECTION);
}

double magnesium_radio_ctrl_impl::get_rx_bandwidth(
    const size_t chan
) /* const */ {
    return _myk_get_bandwidth(chan, RX_DIRECTION);
}

size_t magnesium_radio_ctrl_impl::get_chan_from_dboard_fe(
    const std::string &fe, const direction_t dir
) {
    // UHD_LOG_TRACE("MAGNESIUM", "get_chan_from_dboard_fe " << fe << " returns " << boost::lexical_cast<size_t>(fe));
    return boost::lexical_cast<size_t>(fe);
}

std::string magnesium_radio_ctrl_impl::get_dboard_fe_from_chan(
    const size_t chan,
    const direction_t dir
) {
    // UHD_LOG_TRACE("MAGNESIUM", "get_dboard_fe_from_chan " << chan << " returns " << std::to_string(chan));
    return std::to_string(chan);
}

double magnesium_radio_ctrl_impl::get_output_samp_rate(size_t port)
{
    return MAGNESIUM_RADIO_RATE;
}

void magnesium_radio_ctrl_impl::set_rpc_client(
    uhd::rpc_client::sptr rpcc,
    const uhd::device_addr_t &block_args
) {
    _rpcc = rpcc;
    _block_args = block_args;

    // EEPROM paths subject to change FIXME
    const size_t db_idx = get_block_id().get_block_count();
    _tree->access<eeprom_map_t>(_root_path / "eeprom")
        .add_coerced_subscriber([this, db_idx](const eeprom_map_t& db_eeprom){
            this->_rpcc->notify_with_token("set_db_eeprom", db_idx, db_eeprom);
        })
        .set_publisher([this, db_idx](){
            return this->_rpcc->request_with_token<eeprom_map_t>(
                "get_db_eeprom", db_idx
            );
        })
    ;
}

/******************************************************************************
 * Helpers
 *****************************************************************************/
fs_path magnesium_radio_ctrl_impl::_get_fe_path(size_t chan, direction_t dir)
{
    switch (dir)
    {
        case TX_DIRECTION:
            return fs_path("dboards" / _radio_slot / "tx_frontends" / get_dboard_fe_from_chan(chan, TX_DIRECTION));
        case RX_DIRECTION:
            return fs_path("dboards" / _radio_slot / "rx_frontends" / get_dboard_fe_from_chan(chan, RX_DIRECTION));
        default:
            UHD_THROW_INVALID_CODE_PATH();
    }
}

void magnesium_radio_ctrl_impl::_update_atr_switches(
    const size_t chan,
    const direction_t dir,
    const std::string &ant
){
    magnesium_cpld_ctrl::rx_sw1_t rx_sw1  = magnesium_cpld_ctrl::RX_SW1_RX2INPUT;
    magnesium_cpld_ctrl::sw_trx_t sw_trx = magnesium_cpld_ctrl::SW_TRX_FROMLOWERFILTERBANKTXSW1;

    bool trx_led = false, rx2_led = true;
    //bool tx_pa_enb = true, tx_amp_enb = true, tx_myk_en=true;
    if (ant == "TX/RX" && dir== RX_DIRECTION)
    {
        rx_sw1 = magnesium_cpld_ctrl::RX_SW1_TRXSWITCHOUTPUT;
        sw_trx = magnesium_cpld_ctrl::SW_TRX_RXCHANNELPATH;
        trx_led = true;
        rx2_led = false;
    }
    UHD_LOG_TRACE("MAGNESIUM", "Update all atr related switches for " << dir << " " << ant );
    if (dir == RX_DIRECTION){
        _cpld->set_rx_atr_bits(
            magnesium_cpld_ctrl::chan_sel_t(chan),
            magnesium_cpld_ctrl::ON,
            rx_sw1,
            trx_led,
            rx2_led,
            true,
            true,
            true,
            true
        );
        _cpld->set_tx_atr_bits(
            magnesium_cpld_ctrl::chan_sel_t(chan),
            magnesium_cpld_ctrl::IDLE,
            false,
            sw_trx,
            false,
            false,
            false
        );
        _cpld->set_rx_atr_bits(
            chan,
            magnesium_cpld_ctrl::IDLE,
            rx_sw1,
            false,
            false,
            false,
            false,
            false,
            true
        );

    }
}

void magnesium_radio_ctrl_impl::_update_freq_switches(
    const double freq,
    const size_t chan,
    const direction_t dir
){
    UHD_LOG_TRACE("MAGNESIUM", "Update all freq related switches for " << freq);
     // Set filters based on frequency
     if (dir == RX_DIRECTION) {
        if (freq < MAGNESIUM_RX_BAND1_MIN_FREQ) {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_LOWERFILTERBANKTOSWITCH3,
                magnesium_cpld_ctrl::RX_SW3_FILTER0490LPMHZ,
                magnesium_cpld_ctrl::RX_SW4_FILTER2700HPMHZ,
                magnesium_cpld_ctrl::RX_SW5_FILTER0490LPMHZFROM,
                magnesium_cpld_ctrl::RX_SW6_LOWERFILTERBANKFROMSWITCH5,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_LOBAND,
                true
            );
        } else if (freq < MAGNESIUM_RX_BAND2_MIN_FREQ) {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_LOWERFILTERBANKTOSWITCH3,
                magnesium_cpld_ctrl::RX_SW3_FILTER0440X0530MHZ,
                magnesium_cpld_ctrl::RX_SW4_FILTER2700HPMHZ,
                magnesium_cpld_ctrl::RX_SW5_FILTER0440X0530MHZFROM,
                magnesium_cpld_ctrl::RX_SW6_LOWERFILTERBANKFROMSWITCH5,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_BYPASS,
                false
            );
        } else if (freq < MAGNESIUM_RX_BAND3_MIN_FREQ) {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_LOWERFILTERBANKTOSWITCH3,
                magnesium_cpld_ctrl::RX_SW3_FILTER0650X1000MHZ,
                magnesium_cpld_ctrl::RX_SW4_FILTER2700HPMHZ,
                magnesium_cpld_ctrl::RX_SW5_FILTER0650X1000MHZFROM,
                magnesium_cpld_ctrl::RX_SW6_LOWERFILTERBANKFROMSWITCH5,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_BYPASS,
                false
            );
        } else if (freq < MAGNESIUM_RX_BAND4_MIN_FREQ) {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_LOWERFILTERBANKTOSWITCH3,
                magnesium_cpld_ctrl::RX_SW3_FILTER1100X1575MHZ,
                magnesium_cpld_ctrl::RX_SW4_FILTER2700HPMHZ,
                magnesium_cpld_ctrl::RX_SW5_FILTER1100X1575MHZFROM,
                magnesium_cpld_ctrl::RX_SW6_LOWERFILTERBANKFROMSWITCH5,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_BYPASS,
                false
            );
        } else if (freq < MAGNESIUM_RX_BAND5_MIN_FREQ) {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_LOWERFILTERBANKTOSWITCH3,
                magnesium_cpld_ctrl::RX_SW3_FILTER1600X2250MHZ,
                magnesium_cpld_ctrl::RX_SW4_FILTER1600X2250MHZFROM,
                magnesium_cpld_ctrl::RX_SW5_FILTER0440X0530MHZFROM,
                magnesium_cpld_ctrl::RX_SW6_UPPERFILTERBANKFROMSWITCH4,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_BYPASS,
                false
            );
        } else if (freq < MAGNESIUM_RX_BAND6_MIN_FREQ) {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_LOWERFILTERBANKTOSWITCH3,
                magnesium_cpld_ctrl::RX_SW3_FILTER2100X2850MHZ,
                magnesium_cpld_ctrl::RX_SW4_FILTER2100X2850MHZFROM,
                magnesium_cpld_ctrl::RX_SW5_FILTER0440X0530MHZFROM,
                magnesium_cpld_ctrl::RX_SW6_UPPERFILTERBANKFROMSWITCH4,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_BYPASS,
                false
            );
        } else {
            _cpld->set_rx_switches(
                magnesium_cpld_ctrl::BOTH,
                magnesium_cpld_ctrl::RX_SW2_UPPERFILTERBANKTOSWITCH4,
                magnesium_cpld_ctrl::RX_SW3_SHUTDOWNSW3,
                magnesium_cpld_ctrl::RX_SW4_FILTER2700HPMHZ,
                magnesium_cpld_ctrl::RX_SW5_FILTER0440X0530MHZFROM,
                magnesium_cpld_ctrl::RX_SW6_UPPERFILTERBANKFROMSWITCH4,
                magnesium_cpld_ctrl::LOWBAND_MIXER_PATH_SEL_BYPASS,
                false
            );
        }
    } else {
        if (freq < MAGNESIUM_TX_BAND1_MIN_FREQ) {
        } else if (freq < MAGNESIUM_TX_BAND2_MIN_FREQ) {
        } else if (freq < MAGNESIUM_TX_BAND3_MIN_FREQ) {
        } else if (freq < MAGNESIUM_TX_BAND4_MIN_FREQ) {
        } else {
        }
    }
    UHD_LOG_INFO("MAGNESIUM", "Update all freq related switches for " << freq <<" finished!.");
}
/******************************************************************************
 * AD9371 Controls
 *****************************************************************************/
double magnesium_radio_ctrl_impl::_myk_set_frequency(
        const double freq,
        const size_t chan,
        const direction_t dir
) {


    // Note: There is only one LO per RX or TX, so changing frequency will
    // affect the adjacent channel in the same direction. We have to make sure
    // that getters will always tell the truth!
    auto which = _get_which(dir,_radio_slot);
    UHD_LOG_TRACE("MAGNESIUM", "requested frequency of " << freq);
    _update_freq_switches(freq,chan,dir);
    double ad9371_freq = freq;
    auto lo_iface = (dir == RX_DIRECTION) ? _rx_lo : _tx_lo;

    if (freq < MAGNESIUM_LOWBAND_FREQ) { // Low band
        UHD_LOG_WARNING("LO BAND", "requested frequency of " << freq);
        double if_freq = (dir == RX_DIRECTION) ? MAGNESIUM_RX_IF_FREQ
                                               : MAGNESIUM_TX_IF_FREQ;
        double lo_freq = if_freq - freq;
        _lo_set_frequency(lo_iface, lo_freq, chan);
        lo_iface->set_output_enable(adf435x_iface::RF_OUTPUT_A, true); // TODO: Find correct value
        lo_iface->set_output_enable(adf435x_iface::RF_OUTPUT_B, true); // TODO: Find correct value
        lo_iface->commit();
        ad9371_freq = if_freq;
    } else {
        UHD_LOG_WARNING("HI BAND", "requested frequency of " << freq);
        lo_iface->set_output_enable(adf435x_iface::RF_OUTPUT_A, false); // TODO: Find correct value
        lo_iface->set_output_enable(adf435x_iface::RF_OUTPUT_B, false); // TODO: Find correct value
        lo_iface->commit();
    }

    UHD_LOG_TRACE("MAGNESIUM",
            "Calling " << _rpc_prefix << "set_freq on " << which << " with " << ad9371_freq);
    auto retval = _rpcc->request_with_token<double>(_rpc_prefix + "set_freq", which, ad9371_freq, false);
    UHD_LOG_TRACE("MAGNESIUM",
            _rpc_prefix << "set_freq returned " << retval);


    return retval;
}

double magnesium_radio_ctrl_impl::_myk_set_gain(
        const double gain,
        const size_t chan,
        const direction_t dir
) {
    auto which = _get_which(dir, chan);
    UHD_LOG_TRACE("MAGNESIUM", "Calling " << _rpc_prefix << "set_gain on " << which << " with " << gain);
    auto retval = _rpcc->request_with_token<double>(_rpc_prefix + "set_gain", which, gain);
    UHD_LOG_TRACE("MAGNESIUM", _rpc_prefix << "set_gain returned " << retval);
    return retval;
}

void magnesium_radio_ctrl_impl::_myk_set_antenna(
        const std::string &ant,
        const size_t chan,
        const direction_t dir
) {
    // TODO: implement
    UHD_LOG_WARNING("MAGNESIUM", "Attempting to set antenna " << ant << " " << chan << " " << dir );
    _update_atr_switches(chan,dir,ant);
    // CPLD control?
}

double magnesium_radio_ctrl_impl::_myk_set_bandwidth(const double bandwidth, const size_t chan, const direction_t dir)
{
    // TODO: implement
    UHD_LOG_WARNING("MAGNESIUM", "Ignoring attempt to set bandwidth");
    return get_rx_bandwidth(chan);
}

double magnesium_radio_ctrl_impl::_myk_get_frequency(const size_t chan, const direction_t dir)
{
    auto which = _get_which(dir, chan);
    UHD_LOG_TRACE("MAGNESIUM", "calling " << _rpc_prefix << "get_freq on " << which);
    auto retval = _rpcc->request_with_token<double>(_rpc_prefix + "get_freq", which);
    UHD_LOG_TRACE("MAGNESIUM", _rpc_prefix << "get_freq returned " << retval);
    return retval;
}

double magnesium_radio_ctrl_impl::_myk_get_gain(const size_t chan, const direction_t dir)
{
    auto which = _get_which(dir, chan);
    UHD_LOG_TRACE("MAGNESIUM", "calling " << _rpc_prefix << "get_gain on " << which);
    auto retval = _rpcc->request_with_token<double>(_rpc_prefix + "get_gain", which);
    UHD_LOG_TRACE("MAGNESIUM", _rpc_prefix << "get_gain returned " << retval);
    return retval;
}

std::string magnesium_radio_ctrl_impl::_myk_get_antenna(const size_t chan, const direction_t dir)
{
    // TODO: implement
    UHD_LOG_WARNING("MAGNESIUM", "Ignoring attempt to get antenna");
    return "RX1";
    // CPLD control?
}

double magnesium_radio_ctrl_impl::_myk_get_bandwidth(const size_t chan, const direction_t dir)
{
    // TODO: implement
    UHD_LOG_WARNING("MAGNESIUM", "Ignoring attempt to get bandwidth");
    return MAGNESIUM_DEFAULT_BANDWIDTH;
}

/******************************************************************************
 * ADF4351 Controls
 *****************************************************************************/
double magnesium_radio_ctrl_impl::_lo_set_frequency(
        adf435x_iface::sptr lo_iface,
        const double freq,
        const size_t chan
) {

        UHD_LOG_TRACE("MAGNESIUM", "attempting to tune low band LO to " << freq);
        lo_iface->set_feedback_select(adf435x_iface::FB_SEL_DIVIDED);
        lo_iface->set_reference_freq(100e6); // FIXME: How to get refclk freq? This can change.
        lo_iface->set_prescaler(adf435x_iface::PRESCALER_4_5);

        double actual_freq = 0.0;
        actual_freq = lo_iface->set_frequency(freq, false); // FIXME: always fractional-n mode
        UHD_LOG_TRACE("MAGNESIUM", "actual low band LO is " << actual_freq);
        lo_iface->set_output_power(adf435x_iface::RF_OUTPUT_A, adf435x_iface::OUTPUT_POWER_2DBM); // TODO: Find correct value
        lo_iface->set_output_power(adf435x_iface::RF_OUTPUT_B, adf435x_iface::OUTPUT_POWER_2DBM); // TODO: Find correct value
        lo_iface->set_charge_pump_current(adf435x_iface::CHARGE_PUMP_CURRENT_0_31MA);

        // TODO: Check for PLL lock
        //sleep(1);
        //auto lock_det = _rpcc->request_with_token<uint16_t>(_slot_prefix + "cpld_peek", 0x12);
        //UHD_LOG_TRACE("MAGNESIUM", "lock detect is " << lock_det);

	return actual_freq;
}

UHD_RFNOC_BLOCK_REGISTER(magnesium_radio_ctrl, "MagnesiumRadio");
