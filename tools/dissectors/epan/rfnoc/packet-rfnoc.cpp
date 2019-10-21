/*
 * Dissector for UHD RFNoC (CHDR) packets
 *
 * Copyright 2019 Ettus Research, a National Instruments brand
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <epan/packet.h>

#ifdef __cplusplus
}
#endif

#include <cstdio>
#include <iostream>

#include "../../../../host/lib/usrp/x300/x300_fw_common.h"
#include <uhdlib/rfnoc/chdr_packet.hpp>

constexpr unsigned int RFNOC_PORT = X300_VITA_UDP_PORT;
static const uhd::rfnoc::chdr::chdr_packet_factory pkt_factory(uhd::rfnoc::CHDR_W_64, uhd::ENDIANNESS_LITTLE);

static int proto_rfnoc = -1;
static int hf_rfnoc_hdr = -1;
static int hf_rfnoc_hdr_vc = -1;
static int hf_rfnoc_hdr_eob = -1;
static int hf_rfnoc_hdr_eov = -1;
static int hf_rfnoc_hdr_pkttype = -1;
static int hf_rfnoc_hdr_num_mdata = -1;
static int hf_rfnoc_hdr_seqnum = -1;
static int hf_rfnoc_hdr_len = -1;
static int hf_rfnoc_hdr_dst_epid = -1;
static int hf_rfnoc_timestamp = -1;
static int hf_rfnoc_src_epid = -1;
static int hf_rfnoc_ctrl = -1;
static int hf_rfnoc_ctrl_dst_port = -1;
static int hf_rfnoc_ctrl_src_port = -1;
static int hf_rfnoc_ctrl_num_data = -1;
static int hf_rfnoc_ctrl_seqnum = -1;
static int hf_rfnoc_ctrl_is_ack = -1;
static int hf_rfnoc_ctrl_address = -1;
static int hf_rfnoc_ctrl_data0 = -1;
static int hf_rfnoc_ctrl_data = -1; // TODO: Figure out what to do here
static int hf_rfnoc_ctrl_byte_enable = -1;
static int hf_rfnoc_ctrl_opcode = -1;
static int hf_rfnoc_ctrl_status = -1;
static int hf_rfnoc_strs = -1;
static int hf_rfnoc_strs_status = -1;
static int hf_rfnoc_strs_capacity_bytes = -1;
static int hf_rfnoc_strs_capacity_pkts = -1;
static int hf_rfnoc_strs_xfer_bytes = -1;
static int hf_rfnoc_strs_xfer_pkts = -1;
static int hf_rfnoc_strs_buff_info = -1;
static int hf_rfnoc_strs_ext_status = -1;
static int hf_rfnoc_strc = -1;
static int hf_rfnoc_strc_opcode = -1;
static int hf_rfnoc_strc_data = -1;
static int hf_rfnoc_strc_num_pkts = -1;
static int hf_rfnoc_strc_num_bytes = -1;
static int hf_rfnoc_mgmt = -1;
static int hf_rfnoc_mgmt_protover = -1;
static int hf_rfnoc_mgmt_chdr_w = -1;
static int hf_rfnoc_mgmt_num_hops = -1;
static int hf_rfnoc_mgmt_hop = -1;
static int hf_rfnoc_mgmt_op = -1;
static int hf_rfnoc_mgmt_op_code = -1;
static int hf_rfnoc_mgmt_op_dest = -1;
static int hf_rfnoc_mgmt_op_device_id = -1;
static int hf_rfnoc_mgmt_op_node_type = -1;
static int hf_rfnoc_mgmt_op_node_inst = -1;
static int hf_rfnoc_mgmt_op_cfg_address = -1;
static int hf_rfnoc_mgmt_op_cfg_data = -1;

static const value_string RFNOC_PACKET_TYPES[] = {
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_MGMT, "Management" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_STRS, "Stream Status" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_STRC, "Stream Command" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_CTRL, "Control" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_NO_TS, "Data" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_WITH_TS, "Data with Timestamp" },
};

static const value_string RFNOC_PACKET_TYPES_SHORT[] = {
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_MGMT, "mgmt" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_STRS, "strs" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_STRC, "strc" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_CTRL, "ctrl" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_NO_TS, "data" },
    { uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_WITH_TS, "data" },
};

static const value_string RFNOC_CTRL_STATUS[] = {
    { uhd::rfnoc::chdr::ctrl_status_t::CMD_OKAY, "OK" },
    { uhd::rfnoc::chdr::ctrl_status_t::CMD_CMDERR, "CMDERR" },
    { uhd::rfnoc::chdr::ctrl_status_t::CMD_TSERR, "TSERR" },
    { uhd::rfnoc::chdr::ctrl_status_t::CMD_WARNING, "WARNING" },
};

static const value_string RFNOC_CTRL_OPCODES[] = {
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_SLEEP, "sleep" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_WRITE, "write" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_READ, "read" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_READ_WRITE, "r/w" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_BLOCK_WRITE, "block write" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_BLOCK_READ, "block read" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_POLL, "poll" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_USER1, "user1" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_USER2, "user2" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_USER3, "user3" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_USER4, "user4" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_USER5, "user5" },
    { uhd::rfnoc::chdr::ctrl_opcode_t::OP_USER6, "user6" },
};

static const value_string RFNOC_STRS_STATUS[] = {
    { uhd::rfnoc::chdr::strs_status_t::STRS_OKAY, "OK" },
    { uhd::rfnoc::chdr::strs_status_t::STRS_CMDERR, "CMDERR" },
    { uhd::rfnoc::chdr::strs_status_t::STRS_SEQERR, "SEQERR" },
    { uhd::rfnoc::chdr::strs_status_t::STRS_DATAERR, "DATAERR" },
    { uhd::rfnoc::chdr::strs_status_t::STRS_RTERR, "RTERR" },
};

static const value_string RFNOC_STRC_OPCODES[] = {
    { uhd::rfnoc::chdr::strc_op_code_t::STRC_INIT, "init" },
    { uhd::rfnoc::chdr::strc_op_code_t::STRC_PING, "ping" },
    { uhd::rfnoc::chdr::strc_op_code_t::STRC_RESYNC, "resync" },
};

static const value_string RFNOC_MGMT_OPCODES[] = {
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_NOP, "nop" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_ADVERTISE, "advertise" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_SEL_DEST, "select_dest" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_RETURN, "return_to_sender" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_INFO_REQ, "node_info_req" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_INFO_RESP, "node_info_resp" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_CFG_WR_REQ, "cfg_wr_req" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_CFG_RD_REQ, "cfg_rd_req" },
    { uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_CFG_RD_RESP, "cfg_rd_resp" },
};

/* the heuristic dissector is called on every packet with payload.
 * The warning printed for this should only be printed once.
 */
static int heur_warning_printed = 0;

/* Subtree handles: set by register_subtree_array */
static gint ett_rfnoc = -1;
static gint ett_rfnoc_hdr = -1;
static gint ett_rfnoc_ctrl = -1;
static gint ett_rfnoc_strs = -1;
static gint ett_rfnoc_strc = -1;
static gint ett_rfnoc_mgmt = -1;
static gint ett_rfnoc_mgmt_hop = -1;
static gint ett_rfnoc_mgmt_hop_op = -1;

/* Forward-declare the dissector functions */
static int dissect_rfnoc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data);

/* The dissector itself */
static int dissect_rfnoc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data)
{
    // Here are all the variables
    proto_item *item;
    proto_tree *rfnoc_tree;
    proto_item *header_item;
    proto_tree *header_tree;
    proto_item *ctrl_item;
    proto_tree *ctrl_tree;
    proto_item *strs_item;
    proto_tree *strs_tree;
    proto_item *strc_item;
    proto_tree *strc_tree;
    proto_item *mgmt_item;
    proto_tree *mgmt_tree;
    proto_item *hop_item;
    proto_tree *hop_tree;
    proto_item *mgmt_op_item;
    proto_tree *mgmt_op_tree;
    gint len;

    guint64 hdr;
    gint flag_offset;
    guint8 *bytes;
    guint8 hdr_bits = 0;
    gboolean is_eob = 0;
    gboolean is_eov = 0;
    uint64_t timestamp;
    int chdr_len = 0;
    gboolean is_network;
    gint endianness;
    size_t offset = 0;
    /* FIXME: Assuming CHDR_W_64 */
    size_t chdr_w_bytes = 8;

    if (pinfo->match_uint == RFNOC_PORT) {
        is_network = TRUE;
        flag_offset = 0;
        endianness = ENC_LITTLE_ENDIAN;
    }

    col_set_str(pinfo->cinfo, COL_PROTOCOL, "RFNoC");
    /* Clear out stuff in info column */
    col_clear(pinfo->cinfo, COL_INFO);

    len = tvb_reported_length(tvb);

    if (tree) {
        guint64 hdr = tvb_get_letoh64(tvb, 0);
        uhd::rfnoc::chdr::chdr_header chdr_hdr(hdr);
        chdr_len = chdr_hdr.get_length();
        len = (len < chdr_len) ? len : chdr_len;

        /* Start with a top-level item to add everything else to */
        item = proto_tree_add_item(tree, proto_rfnoc, tvb, offset, len, ENC_NA);

        if (len >= 4) {
            rfnoc_tree = proto_item_add_subtree(item, ett_rfnoc);
            proto_item_append_text(item, ", Packet type: %s, Dst EPID: %d",
                val_to_str(chdr_hdr.get_pkt_type(), RFNOC_PACKET_TYPES, "Unknown (0x%x)"), chdr_hdr.get_dst_epid()
            );
            col_add_fstr(pinfo->cinfo, COL_INFO, "%s dst_epid=%d",
                val_to_str(chdr_hdr.get_pkt_type(), RFNOC_PACKET_TYPES_SHORT, "Unknown (0x%x)"), chdr_hdr.get_dst_epid()
            );

            /* Header info. First, a top-level header tree item: */
            header_item = proto_tree_add_item(rfnoc_tree, hf_rfnoc_hdr, tvb, offset, 8, endianness);
            header_tree = proto_item_add_subtree(header_item, ett_rfnoc_hdr);
            /* Let us query hdr.type */
            proto_tree_add_string(
                header_tree, hf_rfnoc_hdr_pkttype, tvb, offset+6, 1,
                val_to_str(chdr_hdr.get_pkt_type(), RFNOC_PACKET_TYPES_SHORT, "invalid")
            );

            /* Add Dst EPID */
            proto_item_append_text(header_item, ", Dst EPID: %02x", chdr_hdr.get_dst_epid());
            proto_tree_add_uint(header_tree, hf_rfnoc_hdr_dst_epid, tvb, offset, 2, chdr_hdr.get_dst_epid());
            /* Add length */
            proto_tree_add_uint(header_tree, hf_rfnoc_hdr_len, tvb, offset+2, 2, chdr_hdr.get_length());
            /* Add sequence number */
            proto_tree_add_uint(header_tree, hf_rfnoc_hdr_seqnum, tvb, offset+4, 2, chdr_hdr.get_seq_num());

            if (chdr_hdr.get_num_mdata()) {
                /* Can't decode packets with metadata */
                return len;
            }

            /* TODO: Update offsets if there is a timestamp. Also update lengths */
            /* Add subtree based on packet type */
            uhd::rfnoc::chdr::packet_type_t pkttype = chdr_hdr.get_pkt_type();
            offset += 8;
            if (pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_CTRL) {
                ctrl_item = proto_tree_add_item(rfnoc_tree, hf_rfnoc_ctrl, tvb, 8, chdr_len-8, endianness);
                ctrl_tree = proto_item_add_subtree(ctrl_item, ett_rfnoc_ctrl);
                auto pkt = pkt_factory.make_ctrl();
                pkt->refresh(tvb_get_ptr(tvb, 0, chdr_len));
                auto payload = pkt->get_payload();

                /* Add source EPID */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_src_epid, tvb, offset+4, 2, payload.src_epid);
                /* Add source port */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_ctrl_src_port, tvb, offset+1, 2, payload.src_port);
                /* Add dest port */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_ctrl_dst_port, tvb, offset, 2, payload.dst_port);
                /* Add num data */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_ctrl_num_data, tvb, offset+2, 1, payload.data_vtr.size());
                /* Add is_ack */
                proto_tree_add_boolean(ctrl_tree, hf_rfnoc_ctrl_is_ack, tvb, offset+3, 1, payload.is_ack);
                /* Add sequence number */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_ctrl_seqnum, tvb, offset+3, 1, payload.seq_num);
                if (payload.timestamp) {
                    proto_tree_add_uint64(ctrl_tree, hf_rfnoc_timestamp, tvb, offset+8, 8, *payload.timestamp);
                    offset += 8;
                }
                /* Add data0 */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_ctrl_data0, tvb, offset+12, 4, payload.data_vtr[0]);
                /* Add op code */
                proto_tree_add_string(
                    ctrl_tree, hf_rfnoc_ctrl_opcode, tvb, offset+11, 1,
                    val_to_str(payload.op_code, RFNOC_CTRL_OPCODES, "reserved")
                );
                /* Add address */
                proto_tree_add_uint(ctrl_tree, hf_rfnoc_ctrl_address, tvb, offset+8, 3, payload.address);
                /* Add status */
                proto_tree_add_string(
                    ctrl_tree, hf_rfnoc_ctrl_status, tvb, offset+11, 1,
                    val_to_str(payload.status, RFNOC_CTRL_STATUS, "reserved")
                );
            }

            if (pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_STRS) {
                strs_item = proto_tree_add_item(rfnoc_tree, hf_rfnoc_strs, tvb, offset, chdr_len-8, endianness);
                strs_tree = proto_item_add_subtree(strs_item, ett_rfnoc_strs);
                auto pkt = pkt_factory.make_strs();
                pkt->refresh(tvb_get_ptr(tvb, 0, chdr_len));
                auto payload = pkt->get_payload();

                proto_tree_add_string(
                    strs_tree, hf_rfnoc_strs_status, tvb, offset+5, 1,
                    val_to_str(payload.status, RFNOC_STRS_STATUS, "invalid")
                );

                /* Add source EPID */
                proto_tree_add_uint(strs_tree, hf_rfnoc_src_epid, tvb, offset, 2, payload.src_epid);
                /* Add capacities */
                proto_tree_add_uint64(strs_tree, hf_rfnoc_strs_capacity_bytes, tvb, offset+3, 5, payload.capacity_bytes);
                proto_tree_add_uint(strs_tree, hf_rfnoc_strs_capacity_pkts, tvb, offset+8, 3, payload.capacity_pkts);
                /* Add xfer amounts */
                proto_tree_add_uint64(strs_tree, hf_rfnoc_strs_xfer_bytes, tvb, offset+16, 8, payload.xfer_count_bytes);
                proto_tree_add_uint64(strs_tree, hf_rfnoc_strs_xfer_pkts, tvb, offset+11, 5, payload.xfer_count_pkts);
            }

            if (pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_STRC) {
                strc_item = proto_tree_add_item(rfnoc_tree, hf_rfnoc_strc, tvb, 8, chdr_len-8, endianness);
                strc_tree = proto_item_add_subtree(strc_item, ett_rfnoc_strc);
                auto pkt = pkt_factory.make_strc();
                pkt->refresh(tvb_get_ptr(tvb, 0, chdr_len));
                auto payload = pkt->get_payload();

                proto_tree_add_string(
                    strc_tree, hf_rfnoc_strc_opcode, tvb, offset+2, 1,
                    val_to_str(payload.op_code, RFNOC_STRC_OPCODES, "invalid")
                );

                /* Add source EPID */
                proto_tree_add_uint(strc_tree, hf_rfnoc_src_epid, tvb, offset, 2, payload.src_epid);
                /* Add transfer amounts */
                proto_tree_add_uint64(strc_tree, hf_rfnoc_strc_num_bytes, tvb, offset+8, 8, payload.num_bytes);
                proto_tree_add_uint64(strc_tree, hf_rfnoc_strc_num_pkts, tvb, offset+3, 5, payload.num_pkts);
            }

            if (pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_MGMT) {
                mgmt_item = proto_tree_add_item(rfnoc_tree, hf_rfnoc_mgmt, tvb, offset, len-8, endianness);
                mgmt_tree = proto_item_add_subtree(mgmt_item, ett_rfnoc_mgmt);
                auto pkt = pkt_factory.make_mgmt();
                pkt->refresh(tvb_get_ptr(tvb, 0, chdr_len));
                auto payload = pkt->get_payload();
                /* Add source EPID */
                proto_tree_add_uint(mgmt_tree, hf_rfnoc_src_epid, tvb, offset, 2, payload.get_src_epid());
                size_t num_hops = payload.get_num_hops();
                offset += 8;

                /* FIXME: Assuming CHDR_W_64 here */
                for (size_t hop_id = 0; hop_id < num_hops; hop_id++) {
                    auto hop = payload.get_hop(hop_id);
                    size_t num_ops = hop.get_num_ops();
                    hop_item = proto_tree_add_item(mgmt_tree, hf_rfnoc_mgmt_hop, tvb, offset, num_ops*chdr_w_bytes, endianness);
                    hop_tree = proto_item_add_subtree(hop_item, ett_rfnoc_mgmt_hop);
                    for (size_t op_id = 0; op_id < num_ops; op_id++) {
                        auto op = hop.get_op(op_id);
                        auto opcode = op.get_op_code();
                        mgmt_op_item = proto_tree_add_item(hop_tree, hf_rfnoc_mgmt_op, tvb, offset, chdr_w_bytes, endianness);
                        mgmt_op_tree = proto_item_add_subtree(mgmt_op_item, ett_rfnoc_mgmt_hop_op);

                        /* Add op code */
                        proto_tree_add_string(
                            mgmt_op_tree, hf_rfnoc_mgmt_op_code, tvb, offset+1, 1,
                            val_to_str(opcode, RFNOC_MGMT_OPCODES, "invalid")
                        );

                        /* Add op payload */
                        if (opcode == uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_SEL_DEST) {
                            auto opdata = uhd::rfnoc::chdr::mgmt_op_t::sel_dest_payload(
                                op.get_op_payload());
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_dest, tvb, offset+2, 2, opdata.dest);
                        } else if (opcode == uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_INFO_RESP) {
                            auto opdata = uhd::rfnoc::chdr::mgmt_op_t::node_info_payload(
                                op.get_op_payload());
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_device_id, tvb, offset+2, 6, opdata.device_id);
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_node_type, tvb, offset+2, 6, opdata.node_type);
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_node_inst, tvb, offset+2, 6, opdata.node_inst);
                        } else if (opcode == uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_CFG_WR_REQ) {
                            auto opdata = uhd::rfnoc::chdr::mgmt_op_t::cfg_payload(
                                op.get_op_payload());
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_cfg_address, tvb, offset+2, 6, opdata.addr);
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_cfg_data, tvb, offset+2, 6, opdata.data);
                        } else if (opcode == uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_CFG_RD_REQ) {
                            auto opdata = uhd::rfnoc::chdr::mgmt_op_t::cfg_payload(
                                op.get_op_payload());
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_cfg_address, tvb, offset+2, 6, opdata.addr);
                        } else if (opcode == uhd::rfnoc::chdr::mgmt_op_t::op_code_t::MGMT_OP_CFG_RD_RESP) {
                            auto opdata = uhd::rfnoc::chdr::mgmt_op_t::cfg_payload(
                                op.get_op_payload());
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_cfg_address, tvb, offset+2, 6, opdata.addr);
                            proto_tree_add_uint(mgmt_op_tree, hf_rfnoc_mgmt_op_cfg_data, tvb, offset+2, 6, opdata.data);
                        }
                        offset += chdr_w_bytes;
                    }
                }
            }
            if (pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_NO_TS
                || pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_WITH_TS) {
                is_eob = chdr_hdr.get_eob();
                is_eov = chdr_hdr.get_eov();
                proto_tree_add_boolean(
                    rfnoc_tree, hf_rfnoc_hdr_eob, tvb, offset + 7, 1, is_eob);
                proto_tree_add_boolean(
                    rfnoc_tree, hf_rfnoc_hdr_eov, tvb, offset + 7, 1, is_eov);
            }
            if (pkttype == uhd::rfnoc::chdr::packet_type_t::PKT_TYPE_DATA_WITH_TS) {
                auto pkt = pkt_factory.make_generic();
                pkt->refresh(tvb_get_ptr(tvb, 0, chdr_len));
                proto_tree_add_uint64(rfnoc_tree, hf_rfnoc_timestamp, tvb, offset+8, 8, *(pkt->get_timestamp()));
            }
        }
    }
    return len;
}

extern "C"
void proto_register_rfnoc(void)
{
    static hf_register_info hf[] = {
        { &hf_rfnoc_hdr,
            { "Header", "rfnoc.hdr",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_pkttype,
            { "Packet Type", "rfnoc.hdr.pkttype",
                FT_STRINGZ, BASE_NONE,
                NULL, 0x00,
                "Packet Type", HFILL }
        },
        { &hf_rfnoc_hdr_vc,
            { "Virtual Channel", "rfnoc.hdr.vc",
                FT_UINT8, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_eob,
            { "End Of Burst", "rfnoc.hdr.eob",
                FT_BOOLEAN, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_eov,
            { "End Of Vector", "rfnoc.hdr.eov",
                FT_BOOLEAN, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_num_mdata,
            { "Num Mdata", "rfnoc.hdr.nummdata",
                FT_UINT8, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_seqnum,
            { "Sequence Number", "rfnoc.hdr.seqnum",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_len,
            { "RFNoC Length", "rfnoc.hdr.len",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_hdr_dst_epid,
            { "Dst EPID", "rfnoc.hdr.dst_epid",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_timestamp,
            { "Timestamp", "rfnoc.timestamp",
                FT_UINT64, BASE_HEX,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl,
            { "CTRL Payload", "rfnoc.ctrl",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_dst_port,
            { "Dst Port", "rfnoc.ctrl.dst_port",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_src_port,
            { "Src Port", "rfnoc.ctrl.src_port",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_num_data,
            { "Num Data", "rfnoc.ctrl.num_data",
                FT_UINT8, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_data0,
            { "Data0", "rfnoc.ctrl.data0",
                FT_UINT32, BASE_HEX,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_seqnum,
            { "Seq Num", "rfnoc.ctrl.seqnum",
                FT_UINT8, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_is_ack,
            { "Is ACK", "rfnoc.ctrl.is_ack",
                FT_BOOLEAN, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_rfnoc_src_epid,
            { "Src EPID", "rfnoc.src_epid",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_address,
            { "Address", "rfnoc.ctrl.address",
                FT_UINT32, BASE_HEX,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_opcode,
            { "Op Code", "rfnoc.ctrl.opcode",
                FT_STRINGZ, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_ctrl_status,
            { "Status", "rfnoc.ctrl.status",
                FT_STRINGZ, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strs,
            { "Stream Status", "rfnoc.strs",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strs_status,
            { "Status", "rfnoc.strs.status",
                FT_STRINGZ, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strs_capacity_bytes,
            { "Capacity Bytes", "rfnoc.strs.capacity_bytes",
                FT_UINT64, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strs_capacity_pkts,
            { "Capacity Packets", "rfnoc.strs.capacity_pkts",
                FT_UINT32, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strs_xfer_bytes,
            { "Xfer Count Bytes", "rfnoc.strs.xfer_bytes",
                FT_UINT64, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strs_xfer_pkts,
            { "Xfer Count Packets", "rfnoc.strs.xfer_pkts",
                FT_UINT64, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strc,
            { "Stream Command", "rfnoc.strc",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strc_opcode,
            { "Opcode", "rfnoc.strc.opcode",
                FT_STRINGZ, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strc_num_bytes,
            { "Num Bytes", "rfnoc.strc.num_bytes",
                FT_UINT64, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_strc_num_pkts,
            { "Num Packets", "rfnoc.strc.num_pkts",
                FT_UINT64, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt,
            { "Mgmt Xact", "rfnoc.mgmt",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_protover,
            { "Protocol Version", "rfnoc.mgmt.protover",
                FT_UINT16, BASE_HEX,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_chdr_w,
            { "CHDR Width", "rfnoc.mgmt.chdr_w",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_num_hops,
            { "Num Hops", "rfnoc.mgmt.num_hops",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_hop,
            { "Hop", "rfnoc.mgmt.hop",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op,
            { "Operation", "rfnoc.mgmt.op",
                FT_NONE, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_code,
            { "Opcode", "rfnoc.mgmt.op.op_code",
                FT_STRINGZ, BASE_NONE,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_dest,
            { "Destination", "rfnoc.mgmt.op.dest",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_device_id,
            { "Device ID", "rfnoc.mgmt.op.device_id",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_node_type,
            { "Node Type", "rfnoc.mgmt.op.node_type",
                FT_UINT8, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_node_inst,
            { "Node Instance", "rfnoc.mgmt.op.node_inst",
                FT_UINT16, BASE_DEC,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_cfg_address,
            { "Address", "rfnoc.mgmt.op.cfg_address",
                FT_UINT16, BASE_HEX,
                NULL, 0x00,
                NULL, HFILL }
        },
        { &hf_rfnoc_mgmt_op_cfg_data,
            { "Data", "rfnoc.mgmt.op.cfg_data",
                FT_UINT32, BASE_HEX,
                NULL, 0x00,
                NULL, HFILL }
        },

    };

    static gint *ett[] = {
        &ett_rfnoc,
        &ett_rfnoc_hdr,
        &ett_rfnoc_ctrl,
        &ett_rfnoc_strs,
        &ett_rfnoc_strc,
        &ett_rfnoc_mgmt,
        &ett_rfnoc_mgmt_hop,
        &ett_rfnoc_mgmt_hop_op,
    };

    module_t *rfnoc_module;

    proto_rfnoc = proto_register_protocol("UHD RFNoC", "RFNoC", "rfnoc");
    proto_register_field_array(proto_rfnoc, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    rfnoc_module = prefs_register_protocol(proto_rfnoc, NULL);
}

/* Handler registration */
extern "C"
void proto_reg_handoff_rfnoc(void)
{
    static gboolean initialized = FALSE;
    static dissector_handle_t rfnoc_handle;
    static uint16_t current_port = RFNOC_PORT;

    rfnoc_handle = create_dissector_handle(dissect_rfnoc, proto_rfnoc);
    dissector_add_uint_with_preference("udp.port", current_port, rfnoc_handle);
}
