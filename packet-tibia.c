/* packet-tibia.c
 * Routines for Tibia game protocol dissection
 * By Ahmad Fatoum <ahmad@a3f.at>
 * Copyright 2016 Ahmad Fatoum
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#define USE_PORTS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <epan/packet.h>
#include <epan/prefs.h>
#include <epan/uat.h>
#include <epan/conversation.h>
#include <epan/value_string.h>
#include <wsutil/filesystem.h>
#include <wsutil/file_util.h>

/* User Access Table */
struct rsadecrypt_assoc {
    char* ipaddr;
    char* port;
    char* protocol;
    char* keyfile;
    char* password;
};


#define DEBUG
#ifdef DEBUG
#define TRACE(arg) write(1, (arg), sizeof(arg))
#else
#define TRACE
#endif


static void* rsadecrypt_copy_cb(void *dst_, const void *src_, size_t len _U_);
static void rsadecrypt_free_cb(void *r);
static void rsa_parse_uat(void);
static gboolean rsadecrypt_uat_fld_password_chk_cb(void *r _U_, const char *p _U_, guint len _U_, const void *u1 _U_, const void *u2 _U_, char **err);
static gboolean
rsadecrypt_uat_fld_fileopen_chk_cb(void* r _U_, const char* p, guint len _U_, const void* u1 _U_, const void* u2 _U_, char** err);
static gboolean rsadecrypt_uat_fld_port_chk_cb(void* r _U_, const char* p, guint len _U_, const void* u1 _U_, const void* u2 _U_, char** err);
static gboolean rsadecrypt_uat_fld_ip_chk_cb(void* r _U_, const char* p, guint len _U_, const void* u1 _U_, const void* u2 _U_, char** err);
void proto_reg_handoff_tibia(void);

UAT_CSTRING_CB_DEF(rsakeylist_uats,ipaddr,struct rsadecrypt_assoc)
UAT_CSTRING_CB_DEF(rsakeylist_uats,port,struct rsadecrypt_assoc)
UAT_FILENAME_CB_DEF(rsakeylist_uats,keyfile,struct rsadecrypt_assoc)
UAT_CSTRING_CB_DEF(rsakeylist_uats,password,struct rsadecrypt_assoc)


static int decode_xtea = 1;
#include "xtea.h"
#ifdef HAVE_LIBGCRYPT
static int decode_rsa = 1;
#include <gcrypt.h>
#include "rsa.h"

static rsa_t otserv[1];
static guint8
otserv_n[] = {
#include "otserv_keys/n.h"
},
    otserv_e[] = { /* 65537 */
#include "otserv_keys/e.h"
    },
#if 0
    otserv_p[] = {
#include "otserv_keys/p.h"
    },
    otserv_q[] = {
#include "otserv_keys/q.h"
    },
#endif
    otserv_d[] = {
#include "otserv_keys/d.h"
    };
#endif

#define LOGIN_FLAG_GM 0x01

#define CHAR_FLAG_POISON 0x1
#define CHAR_FLAG_FIRE 0x2
#define CHAR_FLAG_ENERGY 0x4
#define CHAR_FLAG_DRUNK 0x8
#define CHAR_FLAG_MANASHIELD 0x10
#define CHAR_FLAG_PARALYZE 0x20
#define CHAR_FLAG_HASTE 0x40
#define CHAR_FLAG_BATTLE 0x80
#define CHAR_FLAG_WATER 0x100
#define CHAR_FLAG_FROZEN 0x200
#define CHAR_FLAG_DAZZLED 0x400
#define CHAR_FLAG_CURSED 0x800

static guint16 ports[] = {7171, 7172};

static gint proto_tibia = -1;
static uat_t *rsadecrypt_uat = NULL;
static struct rsadecrypt_assoc *rsakeylist_uats = NULL;
static guint nrsadecrypt = 0;
static int hf_len = -1;
static int hf_adler32 = -1;
static int hf_type = -1;
static int hf_os = -1;
static int hf_client_version = -1;
static int hf_file_versions = -1;
static int hf_file_version_spr = -1;
static int hf_file_version_dat = -1;
static int hf_file_version_pic = -1;
static int hf_undecoded_rsa_data = -1;
static int hf_undecoded_xtea_data = -1;
static int hf_xtea_key = -1;
static int hf_loginflags_gm = -1;
static int hf_acc_name = -1;
static int hf_char_name = -1;
static int hf_acc_pass = -1;
static int hf_hwinfo = -1;
static int hf_padding = -1;
static int hf_xtea_len = -1;
static int hf_command_body = -1;
static int hf_loginserv_command = -1;
static int hf_gameserv_command = -1;
static int hf_client_command = -1;
static int hf_motd = -1;
static int hf_dlg_error = -1;
static int hf_dlg_info = -1;
static int hf_charlist = -1;
static int hf_charlist_length = -1;
static int hf_charlist_entry_name = -1;
static int hf_charlist_entry_world = -1;
static int hf_charlist_entry_ip = -1;
static int hf_charlist_entry_port = -1;
static int hf_pacc_days = -1;
static int hf_channel_id = -1;
static int hf_channel_name = -1;
static int hf_unknown = -1;
static int hf_char_flag_poison = -1;
static int hf_char_flag_fire = -1;
static int hf_char_flag_energy = -1;
static int hf_char_flag_drunk = -1;
static int hf_char_flag_manashield = -1;
static int hf_char_flag_paralyze = -1;
static int hf_char_flag_haste = -1;
static int hf_char_flag_battle = -1;
static int hf_char_flag_water = -1;
static int hf_char_flag_frozen = -1;
static int hf_char_flag_dazzled = -1;
static int hf_char_flag_cursed = -1;

static int hf_chat_msg = -1;
static int hf_speech_id = -1;


static gint ett_tibia = -1;
static gint ett_file_versions = -1;
static gint ett_command= -1;
static gint ett_charlist= -1;

#define CONVO_ACTIVE 0x01
#define CONVO_USES_ADLER32 0x02
#define CONVO_USES_XTEA 0x04
#define CONVO_XTEA_KNOWN 0x08
#define CONVO_LOGINSERV 0x10
void version_get_flags(unsigned *flags, guint16 version) {
    if (version >= GUINT16_FROM_LE(831))
        *flags |= CONVO_USES_ADLER32;
}
struct tibia_convo{
    guint16 version;
    guint32 xtea_key[4];
    char *char_name;
    unsigned flags;
    guint16 clientport;
    guint16 servport;
};
static struct tibia_convo *tibia_get_convo(packet_info *pinfo)
{
    struct tibia_convo *convo;
    conversation_t * epan_conversation = find_or_create_conversation(pinfo);

    convo = (struct tibia_convo*)conversation_get_proto_data(epan_conversation, proto_tibia);

    if (convo == NULL)
    {
        convo = wmem_new(wmem_file_scope(), struct tibia_convo);
        convo->flags = CONVO_USES_XTEA /*0; FIXME: this should be calculated from client version*/;
        convo->char_name = "";
        /*FIXME: there gotta be a cleaner way..*/
        if (pinfo->srcport >= 0xC000)
        {
            convo->clientport = pinfo->srcport;
            convo->servport = pinfo->destport;
        }else{
            convo->servport = pinfo->srcport;
            convo->clientport = pinfo->destport;
        }
            

        conversation_add_proto_data(epan_conversation, proto_tibia, (void *)convo);
    }
    return convo;
}

enum {CLIENT_GET_CHARLIST = 0x1, CLIENT_SPEAK = 0x96, CLIENT_LOGIN_CHAR = 0xA, CLIENT_PING = 0x1E,
        CLIENT_MOVE_NORTH = 0x65,
        CLIENT_MOVE_EAST = 0x66,
        CLIENT_MOVE_SOUTH = 0x67,
        CLIENT_MOVE_WEST = 0x68,
        CLIENT_MOVE_NE = 0x6A,
        CLIENT_MOVE_SE = 0x6B,
        CLIENT_MOVE_SW = 0x6C,
        CLIENT_MOVE_NW = 0x6D,
        CLIENT_MOVE_ITEM = 0x78
};
static const value_string from_client_packet_types[] = {
    { CLIENT_GET_CHARLIST,      "Charlist request" },
    { CLIENT_LOGIN_CHAR,        "Character login" },
    { CLIENT_SPEAK,        "Speech" },
    { CLIENT_PING,            "Pong" },
    {CLIENT_MOVE_NORTH, "Move north"},
    {CLIENT_MOVE_EAST, "Move east"},
    {CLIENT_MOVE_SOUTH, "Move south"},
    {CLIENT_MOVE_WEST, "Move west"},
    {CLIENT_MOVE_NE, "Move north-east"},
    {CLIENT_MOVE_SE, "Move south-east"},
    {CLIENT_MOVE_SW, "Move south-west"},
    {CLIENT_MOVE_NW, "Move north-west"},
    {CLIENT_MOVE_ITEM, "Move item"},
    { 0, NULL }
};

enum { LOGINSERV_DLG_ERROR = 0x0A, LOGINSERV_DLG_MOTD = 0x14, LOGINSERV_DLG_CHARLIST = 0x64 };
static const value_string from_loginserv_packet_types[] = {
    { LOGINSERV_DLG_MOTD,       "MOTD" },
    { LOGINSERV_DLG_CHARLIST,   "Charlist" },
    { LOGINSERV_DLG_ERROR,   "Error" },
    { 0, NULL }
};

enum { GAMESERV_DLG_ERROR = 0x14, GAMESERV_PLAYER_CONDITION = 0xA2, GAMESERV_DLG_INFO = 0x15, GAMESERV_PING = 0x1E, GAMESERV_CREATURE_MOVE = 0x6D, GAMESERV_CREATURE_HEALTH = 0x8C, GAMESERV_MAGIC_EFFECT = 0x83, GAMESERV_SPEAK = 0xAA, GAMESERV_CHANNEL_OPEN = 0xAC};
static const value_string from_gameserv_packet_types[] = {
    { GAMESERV_PING,            "Ping" },
    { GAMESERV_DLG_ERROR,       "Error" },
    { GAMESERV_DLG_INFO,        "Info"},
    { GAMESERV_CREATURE_MOVE,        "Creature move"},
    { GAMESERV_PLAYER_CONDITION,        "Player condition"},
    { GAMESERV_CREATURE_HEALTH,        "Creature Health"},
    { GAMESERV_MAGIC_EFFECT,        "Magic effect"},
    { GAMESERV_CHANNEL_OPEN,        "Channel open"},
    { GAMESERV_SPEAK,        "Creature speech"},
    { 0, NULL }
};

static int tibia_dissect_loginserv_packet(struct tibia_convo *convo, tvbuff_t *tvb, packet_info *pinfo, proto_tree *mytree);
static int tibia_dissect_gameserv_packet(struct tibia_convo *convo, tvbuff_t *tvb, packet_info *pinfo, proto_tree *mytree);
static int tibia_dissect_client_packet(struct tibia_convo *convo, tvbuff_t *tvb, packet_info *prinfo, proto_tree *mytree);

static int
dissect_tibia(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* unknown)
{
    struct tibia_convo *convo;
    enum {GET_CHARLIST, CHAR_LOGIN, PING, MAP_PACKET, GAME_PACKET } kind = GAME_PACKET; 
    static char *kinds[] = {"GET_CHARLIST", "CHAR_LOGIN", "PING", "MAP_PACKET", "GAME_PACKET"};
    tvbuff_t *tvb_decrypted;
    int offset = 0, len = 0;
    proto_tree *mytree = NULL, *subtree = NULL;
    proto_item *ti = NULL, *subti = NULL;
    guint16 plen = tvb_get_guint16(tvb, 0, ENC_LITTLE_ENDIAN);
    guint8 ptype = tvb_get_guint16(tvb, 6, ENC_LITTLE_ENDIAN);
    (void)unknown; (void)len;

    convo = tibia_get_convo(pinfo);

    /*FIXME: if announced length != real length it's not a tibia packet?*/
    if (tvb_captured_length_remaining(tvb, 2) != plen)
        return -1;

    /*fixme check client before doing this shit*/
    if (plen == 149 && ptype == CLIENT_GET_CHARLIST)
        kind = GET_CHARLIST;
    else if (plen == 137 && ptype == CLIENT_LOGIN_CHAR)
        kind = CHAR_LOGIN;


    if (pinfo->srcport == convo->servport)
        col_set_str(pinfo->cinfo, COL_PROTOCOL, "Tibia server");
    else
        col_set_str(pinfo->cinfo, COL_PROTOCOL, "Tibia client");

    /* Clear out stuff in the info column */
    /*col_clear(pinfo->cinfo,COL_INFO);*/
    /*col_add_fstr(pinfo->cinfo, COL_INFO, "%s", kinds[kind]);*/
    (void)kinds;
    if (!tree) /* we are not being asked for details */
        return offset;

    ti = proto_tree_add_item(tree, proto_tibia, tvb, 0, -1, ENC_NA);
    mytree = proto_item_add_subtree(ti, ett_tibia);
    proto_tree_add_item(mytree, hf_len, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(mytree, hf_adler32, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;
    switch (kind) {
        default:

            if (((convo->flags & CONVO_USES_XTEA) && !(convo->flags & CONVO_XTEA_KNOWN)) || !decode_xtea)
            {
                proto_tree_add_item(mytree, hf_undecoded_xtea_data, tvb, offset, plen - offset, ENC_NA);
                return offset;
            }

            tvb_decrypted = tvb;
            len = tvb_captured_length_remaining(tvb, offset);
            if (convo->flags & CONVO_USES_XTEA) {
                guint32 *decrypted_buffer;
                len = tvb_captured_length_remaining(tvb, offset);
                if (len % 8 != 0)
                    return -1;
                /* copying and then overwriting might seem a waste at first
                 * but it's required as not to break strict aliasing */
                decrypted_buffer = (guint32*)g_memdup(tvb_get_ptr(tvb, offset, -1), len);
                tibia_xtea_ecb_decrypt(decrypted_buffer, len, convo->xtea_key); /*FIXME: endianness issues in key*/

                /*TODO: check if failure*/
                tvb_decrypted = tvb_new_child_real_data(tvb, (guint8*)decrypted_buffer, len, len);
                tvb_set_free_cb(tvb_decrypted, g_free);
                add_new_data_source(pinfo, tvb_decrypted, "Decrypted Game Data");
            }

            if (pinfo->srcport == convo->servport && (convo->flags & CONVO_LOGINSERV))
                return tibia_dissect_loginserv_packet(convo, tvb_decrypted, pinfo, mytree);
            else if (pinfo->srcport == convo->servport)
                return tibia_dissect_gameserv_packet(convo, tvb_decrypted, pinfo, mytree);
            else
                return tibia_dissect_client_packet(convo, tvb_decrypted, pinfo, mytree);
        case GET_CHARLIST:
            convo->flags |= CONVO_LOGINSERV;
        case CHAR_LOGIN:
            proto_tree_add_item(mytree, hf_type, tvb, offset, 1, ENC_LITTLE_ENDIAN);
            offset += 1;
            proto_tree_add_item(mytree, hf_os, tvb, offset, 2, ENC_LITTLE_ENDIAN);
            offset += 2;
            convo->version = tvb_get_guint16(tvb, offset, ENC_LITTLE_ENDIAN);
            version_get_flags(&convo->flags, convo->version);
            proto_tree_add_item(mytree, hf_client_version, tvb, offset, 2, ENC_LITTLE_ENDIAN);
            offset += 2;
            if (kind == GET_CHARLIST) {
                subti = proto_tree_add_item(mytree, hf_file_versions, tvb, offset, 12, ENC_NA);
                subtree = proto_item_add_subtree(subti, ett_file_versions);
                proto_tree_add_item(subtree, hf_file_version_spr, tvb, offset, 4, ENC_BIG_ENDIAN);
                offset += 4;
                proto_tree_add_item(subtree, hf_file_version_dat, tvb, offset, 4, ENC_BIG_ENDIAN);
                offset += 4;
                proto_tree_add_item(subtree, hf_file_version_pic, tvb, offset, 4, ENC_BIG_ENDIAN);
                offset += 4;
            }

            /* decode RSA */
#ifdef HAVE_LIBGCRYPT
            if (!decode_rsa)
#endif
            {
                proto_tree_add_item(mytree, hf_undecoded_rsa_data, tvb,
                        offset, plen - offset, ENC_NA);
                return offset;
            }

            /* assume OTServ communication */
            /* (TODO: if it fails, mark TCP communication as non-OTServ (or nah it's just once) */
            {
                guint16 rsa_len = tvb_captured_length_remaining(tvb, offset);
                /* FIXME USE WS_MALLOC */
                /*guint8 *decrypted_buffer = (guint8*)wmem_alloc(wmem_packet_scope(), rsa_len);*/
                guint8 *decrypted_buffer = (guint8*)g_malloc(rsa_len);

                 /*TODO: check if failure*/
                rsa_private_decrypt(otserv, tvb_get_ptr(tvb, offset, -1), rsa_len, decrypted_buffer);
                tvb_decrypted = tvb_new_child_real_data(tvb, decrypted_buffer, rsa_len, rsa_len);
                tvb_set_free_cb(tvb_decrypted, g_free);
                add_new_data_source(pinfo, tvb_decrypted, "Decrypted Login Data");
            }
            offset = 0;
            /*FIXME: if first byte != 0 raise error*/
            if (tvb_get_guint8(tvb_decrypted, offset) != 0)
                    return -1;
            offset++;



            tvb_memcpy(tvb_decrypted, convo->xtea_key, offset, 16);
            proto_tree_add_item(mytree, hf_xtea_key, tvb_decrypted, offset, 16, ENC_BIG_ENDIAN);
            offset += 16;
            convo->flags  |= CONVO_XTEA_KNOWN;
            if (kind == CHAR_LOGIN) {
                proto_tree_add_item(mytree, hf_loginflags_gm, tvb_decrypted, offset, 1, ENC_BIG_ENDIAN);
                offset += 1;
            }
            proto_tree_add_item(mytree, hf_acc_name, tvb_decrypted, offset, 2,  ENC_LITTLE_ENDIAN | ENC_ASCII);
            offset += tvb_get_guint16(tvb_decrypted, offset, ENC_LITTLE_ENDIAN) + 2;

            if (kind == CHAR_LOGIN) {
                len = tvb_get_guint16(tvb_decrypted, offset, ENC_LITTLE_ENDIAN) + 2;
                convo->char_name = (char*)tvb_memdup(wmem_file_scope(), tvb_decrypted, offset, len);
                proto_tree_add_item(mytree, hf_char_name, tvb_decrypted, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                offset += len;
            }
            proto_tree_add_item(mytree, hf_acc_pass, tvb_decrypted, offset, 2,  ENC_LITTLE_ENDIAN | ENC_ASCII);
            offset += tvb_get_guint16(tvb_decrypted, offset, ENC_LITTLE_ENDIAN) + 2;

            if (kind == GET_CHARLIST)
                proto_tree_add_item(mytree, hf_hwinfo, tvb_decrypted,
                        offset, -1, ENC_NA);
            else 
                proto_tree_add_item(mytree, hf_padding, tvb_decrypted,
                        offset, -1, ENC_NA);

    }

    return offset;
}

#define CREATE_CMD_SUBTREE(hf, length) \
    do {cmdti = proto_tree_add_item(mytree, hf_command_body, tvb, offset, (length), ENC_NA);\
        cmdtree = proto_item_add_subtree(cmdti, ett_command);\
        proto_tree_add_item(cmdtree, (hf), tvb, offset, 1, ENC_LITTLE_ENDIAN);\
       }while(0)

static int tibia_dissect_loginserv_packet(struct tibia_convo *convo, tvbuff_t *tvb, packet_info *pinfo, proto_tree *mytree)
{
        int offset = 0;
        guint8 cmd;
        guint16 len;
        proto_tree *subtree, *cmdtree = NULL;
        proto_item *cmdti, *subti;
        char *custom = "";
        const char *str;
        (void)convo;(void)pinfo;
        proto_tree_add_item(mytree, hf_xtea_len, tvb, offset, 2, ENC_LITTLE_ENDIAN);
        len = tvb_get_guint16(tvb, offset, ENC_LITTLE_ENDIAN) + 2;
        if (len > tvb_captured_length_remaining(tvb, offset))
            return -1;
        offset += 2;
    cmdti = proto_tree_add_item(mytree, hf_command_body, tvb, offset, -1, ENC_NA);

        while (offset < len) { switch (cmd = tvb_get_guint8(tvb, offset))
        {
            case LOGINSERV_DLG_ERROR:
            case LOGINSERV_DLG_MOTD:
                {
                    guint16 dlg_len = tvb_get_guint16(tvb, offset+1, ENC_LITTLE_ENDIAN);
                    cmdtree = proto_item_add_subtree(cmdti, ett_command);
                    proto_tree_add_item(cmdtree, hf_loginserv_command, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                    proto_item_set_len(cmdti, dlg_len+2);
                    /*CREATE_CMD_SUBTREE(hf_loginserv_command, dlg_len+2);*/
                    offset++;
                    proto_tree_add_item(cmdtree, cmd == LOGINSERV_DLG_MOTD ? hf_motd : hf_dlg_error, tvb, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                    offset += dlg_len + 2;
                }
                break;
            case LOGINSERV_DLG_CHARLIST:
                {
                    guint8 char_count;
                    cmdtree = proto_item_add_subtree(cmdti, ett_command);
                    proto_tree_add_item(cmdtree, hf_loginserv_command, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                    proto_item_set_len(cmdti, len - offset - 1);
                    offset++;
                    char_count = tvb_get_guint8(tvb, offset);
                    proto_tree_add_item(cmdtree, hf_charlist_length, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                    offset++;
                    subti = proto_tree_add_item(cmdtree, hf_charlist, tvb, offset, len - offset - 1, ENC_NA);
                    if (char_count) {
                        subtree = proto_item_add_subtree(subti, ett_charlist);
                        while (char_count --> 0) {
                            proto_tree_add_item(subtree, hf_charlist_entry_name, tvb, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                            offset += tvb_get_guint16(tvb, offset, ENC_LITTLE_ENDIAN) + 2;
                            proto_tree_add_item(subtree, hf_charlist_entry_world, tvb, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                            offset += tvb_get_guint16(tvb, offset, ENC_LITTLE_ENDIAN) + 2;
                            proto_tree_add_item(subtree, hf_charlist_entry_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
                            offset += 4;
                            proto_tree_add_item(subtree, hf_charlist_entry_port, tvb, offset, 2, ENC_LITTLE_ENDIAN);
                            offset += 2;

                        }
                    }

                    proto_tree_add_item(mytree, hf_pacc_days, tvb, offset, 2, ENC_LITTLE_ENDIAN);
                    offset += 2;
                    break;
                }
            default:
                cmdti = proto_tree_add_item(mytree, hf_loginserv_command, tvb, offset, 1, ENC_NA);
                offset++;
                if (len != offset) {
                    proto_tree_add_item(mytree, hf_unknown, tvb,
                            offset, len - offset, ENC_NA);
                }
                offset = len;
        }
        str = try_val_to_str(cmd, from_loginserv_packet_types);
        str = str ? str : "Unknown";
        if (custom)
            proto_item_set_text(cmdti, "Command: %s (0x%x) %s", str, cmd, custom);
        /*TODO: this only shows last packet, should show a csv*/
        col_clear(pinfo->cinfo, COL_INFO);
        col_add_fstr(pinfo->cinfo, COL_INFO, "%s (0x%x)", str, cmd);
        }


        if (tvb_captured_length_remaining(tvb, offset) != 0) {
            proto_tree_add_item(mytree, hf_padding, tvb,
                    offset, -1, ENC_NA);
            offset += tvb_captured_length_remaining(tvb, offset);
        }
        return offset;
}


static int tibia_dissect_gameserv_packet(struct tibia_convo *convo, tvbuff_t *tvb, packet_info *pinfo, proto_tree *mytree)
{
    int offset = 0;
    guint8 cmd;
    guint16 len, clen;
    proto_tree *subtree, *cmdtree = NULL;
    proto_item *subti, *cmdti;
    const char *str;
    char *custom = "";
    (void)convo; (void) subtree; (void)subti;

    proto_tree_add_item(mytree, hf_xtea_len, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    len = tvb_get_guint16(tvb, offset, ENC_LITTLE_ENDIAN) + 2;
    if (len > tvb_captured_length_remaining(tvb, offset))
        return -1;
    offset += 2;
    cmdti = proto_tree_add_item(mytree, hf_command_body, tvb, offset, -1, ENC_NA);

    while (offset < len) { switch (cmd = tvb_get_guint8(tvb, offset))
        {
            case GAMESERV_DLG_ERROR:
            case GAMESERV_DLG_INFO:
                clen = tvb_get_guint16(tvb, offset+1, ENC_LITTLE_ENDIAN) + 2;
                cmdtree = proto_item_add_subtree(cmdti, ett_command);
                proto_tree_add_item(cmdtree, hf_gameserv_command, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                proto_item_set_len(cmdti, clen+2);
                offset++;
                proto_tree_add_item(cmdtree, cmd == GAMESERV_DLG_ERROR ? hf_dlg_error : hf_dlg_info, tvb, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                offset += clen;
                break;
            case GAMESERV_CHANNEL_OPEN:
                clen = tvb_get_guint16(tvb, offset+3, ENC_LITTLE_ENDIAN) + 2;
                cmdtree = proto_item_add_subtree(cmdti, ett_command);
                proto_tree_add_item(cmdtree, hf_gameserv_command, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                proto_item_set_len(cmdti, clen+3);
                offset++;
                proto_tree_add_item(cmdtree, hf_channel_id, tvb, offset, 2, ENC_LITTLE_ENDIAN);
                offset += 2;
                proto_tree_add_item(cmdtree, hf_channel_name, tvb, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                offset += clen;
                break;
            case GAMESERV_PLAYER_CONDITION:
                cmdtree = proto_item_add_subtree(cmdti, ett_command);
                proto_tree_add_item(cmdtree, hf_gameserv_command, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                proto_item_set_len(cmdti, 4);
                offset++;

                proto_tree_add_item(cmdtree, hf_char_flag_poison, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_fire, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_energy, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_drunk, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_manashield, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_paralyze, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_haste, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_battle, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_water, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_frozen, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_dazzled, tvb, offset, 3, ENC_BIG_ENDIAN);
                proto_tree_add_item(cmdtree, hf_char_flag_cursed, tvb, offset, 3, ENC_BIG_ENDIAN);
                offset += 3;
                break;
            case GAMESERV_PING:
                cmdtree = proto_item_add_subtree(cmdti, ett_command);
                proto_tree_add_item(cmdtree, hf_gameserv_command, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                proto_item_set_len(cmdti, 1);
                offset++;
                break;
            default:
                offset++;
                if (len != offset) {
                    proto_tree_add_item(mytree, hf_unknown, tvb,
                            offset, len - offset, ENC_NA);
                
                    offset = len;
                }
        }
        /*FIXME: extract the pointer from cmdti instead, somehoe*/
        str = try_val_to_str(cmd, from_gameserv_packet_types);
        str = str ? str : "Unknown";
        // TODO: show packet hex id only on unknown packets
        if (custom)
            proto_item_set_text(cmdti, "Command: %s (0x%x) %s", str, cmd, custom);
        proto_item_set_len(cmdti, 1);
        /*TODO: this only shows last packet, should show a csv*/
        col_clear(pinfo->cinfo, COL_INFO);
        col_add_fstr(pinfo->cinfo, COL_INFO, "%s (0x%x)", str, cmd);
    }


    if (tvb_captured_length_remaining(tvb, offset) != 0) {
        proto_tree_add_item(mytree, hf_padding, tvb,
                offset, -1, ENC_NA);
        offset += tvb_captured_length_remaining(tvb, offset);
    }
    return offset;
}

static int tibia_dissect_client_packet(struct tibia_convo *convo, tvbuff_t *tvb, packet_info *pinfo, proto_tree *mytree)
{
    int offset = 0;
    guint8 cmd;
    guint16 len, clen;
    proto_tree *subtree, *cmdtree = NULL;
    proto_item *subti, *cmdti;
    const char *str;
    char *custom = "";
    (void)convo; (void) subtree; (void)subti;

    proto_tree_add_item(mytree, hf_xtea_len, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    len = tvb_get_guint16(tvb, offset, ENC_LITTLE_ENDIAN) + 2;
    if (len > tvb_captured_length_remaining(tvb, offset))
        return -1;
    offset += 2;

    while (offset < len) { switch (cmd = tvb_get_guint8(tvb, offset))
        {
            /*case CLIENT_MOVE_ITEM:*/
            case CLIENT_SPEAK:
                clen = tvb_get_guint16(tvb, offset+2, ENC_LITTLE_ENDIAN) + 2;
                CREATE_CMD_SUBTREE(hf_client_command, clen+2);
                offset++;
                proto_tree_add_item(cmdtree, hf_speech_id, tvb, offset, 1, ENC_LITTLE_ENDIAN);
                offset += 1;
                proto_tree_add_item(cmdtree, hf_chat_msg, tvb, offset, 2, ENC_LITTLE_ENDIAN | ENC_ASCII);
                offset += clen;
                break;
            case GAMESERV_PING:
                CREATE_CMD_SUBTREE(hf_client_command, 1);
                offset++;
                break;
            default:
                cmdti = proto_tree_add_item(mytree, hf_client_command, tvb, offset, 1, ENC_NA);
                offset++;
                if (len != offset)
                    proto_tree_add_item(mytree, hf_unknown, tvb,
                            offset, len - offset, ENC_NA);
                offset = len;
        }
        str = try_val_to_str(cmd, from_client_packet_types);
        str = str ? str : "Unknown";
        if (custom)
            proto_item_set_text(cmdti, "Command: %s (0x%x) %s", str, cmd, custom);
            /*TODO: this only shows last packet, should show a csv*/
        col_clear(pinfo->cinfo, COL_INFO);
        col_add_fstr(pinfo->cinfo, COL_INFO, "%s (0x%x)", str, cmd);
    }


    if (tvb_captured_length_remaining(tvb, offset) != 0) {
        proto_tree_add_item(mytree, hf_padding, tvb,
                offset, -1, ENC_NA);
        offset += tvb_captured_length_remaining(tvb, offset);
    }
    return offset;
}

static const value_string operating_systems[] = {
    { 2, "Windows" },
    { 0, NULL }
};
static const value_string speech_ids[] = {
    { 1, "Say" },
    { 2, "Whisper" },
    { 3, "Yell" },
    { 0, NULL }
};

void
proto_register_tibia(void)
{
    static hf_register_info hf[] = {
        { &hf_len,
            { "Packet length", "tibia.len",
                FT_UINT16, BASE_DEC,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_adler32,
            { "Adler32 checksum", "tibia.checksum",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_type,
            { "Packet type", "tibia.type",
                FT_UINT8, BASE_HEX,
                VALS(from_client_packet_types), 0x0,
                NULL, HFILL }
        },
        { &hf_os,
            { "Operating system", "tibia.os",
                FT_UINT16, BASE_HEX,
                VALS(operating_systems), 0x0,
                NULL, HFILL }
        },
        { &hf_client_version,
            { "Client version", "tibia.version",
                FT_UINT16, BASE_DEC,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_file_versions,
            { "File versions", "tibia.version.files",
                FT_NONE, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_file_version_spr,
            { "Tibia.spr version", "tibia.version.spr",
                FT_UINT32, BASE_HEX,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_file_version_dat,
            { "Tibia.dat version", "tibia.version.dat",
                FT_UINT32, BASE_HEX,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_file_version_pic,
            { "Tibia.pic version", "tibia.version.pic",
                FT_UINT32, BASE_HEX,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_undecoded_rsa_data,
            { "RSA Encrypted login data", "tibia.rsa_data",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_undecoded_xtea_data,
            { "XTEA Encrypted game data", "tibia.xtea_data",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_xtea_key,
            { "Symmetric key (XTEA)", "tibia.xtea",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_loginflags_gm,
            { "Gamemaster", "tibia.login.flags.gm",
                FT_BOOLEAN, 8,
                NULL, LOGIN_FLAG_GM,
                NULL, HFILL }
        },
        { &hf_char_flag_poison,
            { "Poison", "tibia.flag.poison",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_POISON,
                NULL, HFILL }
        },
        { &hf_char_flag_fire,
            { "Fire", "tibia.flag.fire",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_FIRE,
                NULL, HFILL }
        },
        { &hf_char_flag_energy,
            { "Energy", "tibia.flag.energy",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_ENERGY,
                NULL, HFILL }
        },
        { &hf_char_flag_drunk,
            { "Drunk", "tibia.flag.drunk",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_DRUNK,
                NULL, HFILL }
        },
        { &hf_char_flag_manashield,
            { "Manashield", "tibia.flag.manashield",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_MANASHIELD,
                NULL, HFILL }
        },
        { &hf_char_flag_paralyze,
            { "Paralyze", "tibia.flag.paralyze",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_PARALYZE,
                NULL, HFILL }
        },
        { &hf_char_flag_haste,
            { "Haste", "tibia.flag.haste",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_HASTE,
                NULL, HFILL }
        },
        { &hf_char_flag_battle,
            { "Battle lock", "tibia.flag.battle",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_BATTLE,
                NULL, HFILL }
        },
        { &hf_char_flag_water,
            { "Drowning", "tibia.flag.water",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_WATER,
                NULL, HFILL }
        },
        { &hf_char_flag_frozen,
            { "Freezing", "tibia.flag.frozen",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_FROZEN,
                NULL, HFILL }
        },
        { &hf_char_flag_dazzled,
            { "Dazzled", "tibia.flag.dazzled",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_DAZZLED,
                NULL, HFILL }
        },
        { &hf_char_flag_cursed,
            { "Cursed", "tibia.flag.cursed",
                FT_BOOLEAN, 24,
                NULL, CHAR_FLAG_CURSED,
                NULL, HFILL }
        },
        { &hf_acc_name,
            { "Account name", "tibia.acc",
                FT_UINT_STRING, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_char_name,
            { "Character name", "tibia.char",
                FT_UINT_STRING, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_acc_pass,
            { "Account password", "tibia.pass",
                FT_UINT_STRING, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_hwinfo,
            { "Hardware info", "tibia.hwinfo",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_padding,
            { "Padding", "tibia.padding",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_xtea_len,
            { "Payload length", "tibia.payload.len",
                FT_UINT16, BASE_DEC,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_command_body,
            { "Command", "tibia.payload",
                FT_BYTES, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_loginserv_command,
            { "Command", "tibia.cmd",
                FT_UINT8, BASE_HEX,
                VALS(from_loginserv_packet_types), 0x0,
                NULL, HFILL }
        },
        { &hf_gameserv_command,
            { "Command", "tibia.cmd",
                FT_UINT8, BASE_HEX,
                VALS(from_gameserv_packet_types), 0x0,
                NULL, HFILL }
        },
        { &hf_client_command,
            { "Command", "tibia.cmd",
                FT_UINT8, BASE_HEX,
                VALS(from_client_packet_types), 0x0,
                NULL, HFILL }
        },
        { &hf_motd,
            { "Message of the day", "tibia.motd",
                FT_UINT_STRING, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_dlg_error,
            { "Error message", "tibia.login.err",
                FT_UINT_STRING, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_dlg_info,
            { "Info message", "tibia.login.info",
                FT_UINT_STRING, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_charlist,
            { "Character list", "tibia.charlist",
                FT_NONE, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_charlist_length,
            { "Character count", "tibia.charlist.count",
                FT_UINT8, BASE_DEC,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_charlist_entry_name,
            { "Character name", "tibia.charlist.name",
                FT_UINT_STRING, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_charlist_entry_world,
            { "World", "tibia.charlist.world",
                FT_UINT_STRING, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_charlist_entry_ip,
            { "IP", "tibia.charlist.ip",
                FT_IPv4, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_charlist_entry_port,
            { "Port", "tibia.charlist.port",
                FT_UINT16, BASE_DEC,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_unknown,
            { "Unknown data", "tibia.unknown",
                FT_BYTES, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_pacc_days,
            { "Premium days left", "tibia.pacc",
                FT_UINT16, BASE_DEC,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_channel_id,
            { "Channel id", "tibia.channel.id",
                FT_UINT8, BASE_HEX,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_channel_name,
            { "Channel name", "tibia.channel",
                FT_UINT_STRING, BASE_NONE,
                NULL, 0x0,
                NULL, HFILL }
        },
        { &hf_speech_id,
            { "Type", "tibia.speechid",
                FT_UINT8, BASE_HEX,
                VALS(speech_ids), 0x0,
                NULL, HFILL }
        },
        { &hf_chat_msg,
            { "Message", "tibia.msg",
                FT_UINT_STRING, BASE_NONE, NULL, 0x0,
                NULL, HFILL }
        },

    };

    module_t *rsa_module;
    static uat_field_t rsakeylist_uats_flds[] = {
        UAT_FLD_CSTRING_OTHER(rsakeylist_uats, ipaddr, "IP address", rsadecrypt_uat_fld_ip_chk_cb, "IPv4 or IPv6 address"),
        UAT_FLD_CSTRING_OTHER(rsakeylist_uats, port, "Port", rsadecrypt_uat_fld_port_chk_cb, "Port Number"),
        UAT_FLD_FILENAME_OTHER(rsakeylist_uats, keyfile, "Key File", rsadecrypt_uat_fld_fileopen_chk_cb, "Private keyfile."),
        UAT_FLD_CSTRING_OTHER(rsakeylist_uats, password,"Password", rsadecrypt_uat_fld_password_chk_cb, "Password (for PCKS#12 keyfile)"),
        UAT_END_FIELDS
    };

    /* Setup protocol subtree array */
    static gint *ett[] = {
        &ett_tibia,
        &ett_command,
        &ett_file_versions,
        &ett_charlist,
    };
    TRACE("initializing Tibia");
    proto_tibia = proto_register_protocol (
            "Tibia Protocol", /* name   */
            "1Tibia",      /* short name */
            "tibia"       /* abbrev     */
            );
    proto_register_field_array(proto_tibia, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));

    rsa_module = prefs_register_protocol(proto_tibia, proto_reg_handoff_tibia);

    rsadecrypt_uat = uat_new("RSA Decrypt",
            sizeof(struct rsadecrypt_assoc),
            "tibia_keys",                     /* filename */
            TRUE,                           /* from_profile */
            &rsakeylist_uats,               /* data_ptr */
            &nrsadecrypt,                   /* numitems_ptr */
            UAT_AFFECTS_DISSECTION,         /* affects dissection of packets, but not set of named fields */
            NULL,                           /* Help section (currently a wiki page) */
            rsadecrypt_copy_cb,
            NULL,
            rsadecrypt_free_cb,
            rsa_parse_uat,
            rsakeylist_uats_flds);
    prefs_register_uat_preference(rsa_module, "key_table",
            "RSA keys list",
            "A table of RSA keys for decrypting protocols newer than 7.8",
            rsadecrypt_uat
            );

    {
#ifdef HAVE_LIBGCRYPT
        struct rsa_params otserv_params = {0,0,0,0,0,0};
        otserv_params.n = otserv_n;
        otserv_params.e = otserv_e;
        otserv_params.d = otserv_d;
        /*otserv_params.p = otserv_p;*/
        /*otserv_params.q = otserv_q;*/
        /*otserv_params.u = otserv_u;*/

        rsa_init(otserv);
        rsa_set_bin_key(otserv, &otserv_params);

#endif
    }
}
static void
rsa_parse_uat(void)
{

}

static void
rsadecrypt_free_cb(void *r)
{
    struct rsadecrypt_assoc *h = (struct rsadecrypt_assoc *)r;

    g_free(h->ipaddr);
    g_free(h->port);
    g_free(h->keyfile);
    g_free(h->password);
}

static void*
rsadecrypt_copy_cb(void *dst_, const void *src_, size_t len _U_)
{
    const struct rsadecrypt_assoc *src = (const struct rsadecrypt_assoc *)src_;
    struct rsadecrypt_assoc       *dst = (struct rsadecrypt_assoc *)dst_;

    dst->ipaddr    = g_strdup(src->ipaddr);
    dst->port      = g_strdup(src->port);
    dst->keyfile   = g_strdup(src->keyfile);
    dst->password  = g_strdup(src->password);

    return dst;
}

static gboolean
rsadecrypt_uat_fld_ip_chk_cb(void* r _U_, const char* p, guint len _U_, const void* u1 _U_, const void* u2 _U_, char** err)
{
    if (!p || strlen(p) == 0u) {
        *err = g_strdup_printf("No IP address given.");
        return FALSE;
    }

    *err = NULL;
    return TRUE;
}

static gboolean
rsadecrypt_uat_fld_port_chk_cb(void* r _U_, const char* p, guint len _U_, const void* u1 _U_, const void* u2 _U_, char** err)
{
    if (!p || strlen(p) == 0u) {
        *err = g_strdup_printf("No Port given.");
        return FALSE;
    }

    if (strcmp(p, "start_tls") != 0){
        const gint i = atoi(p);
        if (i < 0 || i > 65535) {
            *err = g_strdup_printf("Invalid port given.");
            return FALSE;
        }
    }

    *err = NULL;
    return TRUE;
}

static gboolean
rsadecrypt_uat_fld_fileopen_chk_cb(void* r _U_, const char* p, guint len _U_, const void* u1 _U_, const void* u2 _U_, char** err)
{
    ws_statb64 st;

    if (!p || strlen(p) == 0u) {
        printf("no file name\n");
        *err = g_strdup_printf("No filename given.");
        return FALSE;
    } else {
        if (ws_stat64(p, &st) != 0) {
        printf("no exist\n");
            *err = g_strdup_printf("File '%s' does not exist or access is denied.", p);
            return FALSE;
        }
        printf("file exist\n");
    }

    *err = NULL;
    return TRUE;
}

static gboolean
rsadecrypt_uat_fld_password_chk_cb(void *r _U_, const char *p _U_, guint len _U_, const void *u1 _U_, const void *u2 _U_, char **err)
{
#if defined(HAVE_LIBGNUTLS) && defined(HAVE_LIBGCRYPT)

#if 0
    struct rsadecrypt_assoc*  f  = (struct rsadecrypt_assoc *)r;
    FILE                *fp = NULL;
    if (p && (strlen(p) > 0u)) {
        fp = ws_fopen(f->keyfile, "rb");
        if (fp) {
            char *msg = NULL;
            gnutls_x509_privkey_t priv_key = rsa_load_pkcs12(fp, p, &msg);
            if (!priv_key) {
                fclose(fp);
                *err = g_strdup_printf("Could not load PKCS#12 key file: %s", msg);
                g_free(msg);
                return FALSE;
            }
            g_free(msg);
            gnutls_x509_privkey_deinit(priv_key);
            fclose(fp);
        } else {
            *err = g_strdup_printf("Leave this field blank if the keyfile is not PKCS#12.");
            return FALSE;
        }
    }
#endif

    *err = NULL;
    return TRUE;
#else
    *err = g_strdup("Cannot load key files, support is not compiled in.");
    return FALSE;
#endif
}

void
proto_reg_handoff_tibia(void)
{
    static dissector_handle_t tibia_handle;
    unsigned i; (void)i;

    tibia_handle = create_dissector_handle(dissect_tibia, proto_tibia);
#ifdef USE_PORTS
    for (i = 0; i < G_N_ELEMENTS(ports); i++) {
        dissector_add_uint("tcp.port", ports[i], tibia_handle);
    }
#endif
}

