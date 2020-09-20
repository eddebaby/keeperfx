/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packets_misc.c
 *     Processing packets with cheat commands.
 * @par Purpose:
 *     Functions for creating and executing packets.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     20 Sep 2020 - 20 Sep 2020
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "packets.h"

#include "bflib_fileio.h"
#include "bflib_memory.h"
#include "front_landview.h"
#include "game_legacy.h"
#include "game_saves.h"
#include "gui_topmsg.h"


#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define PACKET_TURN_SIZE (NET_PLAYERS_COUNT*sizeof(struct PacketEx) + sizeof(TbBigChecksum))
struct PacketEx bad_packet;
/******************************************************************************/
#ifdef __cplusplus
}
#endif

extern TbBool packets_first_resync;

void set_players_packet_action(struct PlayerInfo *player, unsigned char pcktype,
 unsigned short par1, unsigned short par2, unsigned short par3, unsigned short par4)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    pckt->actn_par1 = par1;
    pckt->actn_par2 = par2;
    pckt->action = pcktype;
}

unsigned char get_players_packet_action(struct PlayerInfo *player)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    return pckt->action;
}

void set_players_packet_control(struct PlayerInfo *player, unsigned long flag)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    pckt->control_flags |= flag;
}


void set_players_add_flag(struct PlayerInfo *player, unsigned long flag)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    pckt->field_10 |= flag;
}

void unset_players_add_flag(struct PlayerInfo *player, unsigned long flag)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    pckt->field_10 &= ~flag;
}

void unset_players_packet_control(struct PlayerInfo *player, unsigned long flag)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    pckt->control_flags &= ~flag;
}

void set_players_packet_position(struct PlayerInfo *player, long x, long y)
{
    struct Packet* pckt = get_packet_direct(player->packet_num);
    pckt->pos_x = x;
    pckt->pos_y = y;
}

/**
 * Gives a pointer for the player's packet.
 * @param plyr_idx The player index for which we want the packet.
 * @return Returns Packet pointer. On error, returns a dummy structure.
 */
struct PacketEx *get_packet_ex(long plyr_idx)
{
    struct PlayerInfo* player = get_player(plyr_idx);
    if (player_invalid(player))
        return INVALID_PACKET;
    if (player->packet_num >= PACKETS_COUNT)
        return INVALID_PACKET;
    return &ex_packets[player->packet_num];
}
/**
 * Gives a pointer to packet of given index.
 * @param pckt_idx Packet index in the array. Note that it may differ from player index.
 * @return Returns Packet pointer. On error, returns a dummy structure.
 */
struct Packet *get_packet_direct(long pckt_idx)
{
    if ((pckt_idx < 0) || (pckt_idx >= PACKETS_COUNT))
        return &INVALID_PACKET->packet;
    return &ex_packets[pckt_idx].packet;
}

struct PacketEx *get_packet_ex_direct(long pckt_idx)
{
    if ((pckt_idx < 0) || (pckt_idx >= PACKETS_COUNT))
        return INVALID_PACKET;
    return &ex_packets[pckt_idx];
}

void clear_packets(void)
{
    for (int i = 0; i < PACKETS_COUNT; i++)
    {
        LbMemorySet(&game.packets_OLD[i], 0, sizeof(struct Packet));
        LbMemorySet(&ex_packets[i], 0, sizeof(struct PacketEx));
    }
}

TbBool open_packet_file_for_load(char *fname, struct CatalogueEntry *centry)
{
    LbMemorySet(centry, 0, sizeof(struct CatalogueEntry));
    strcpy(game.packet_fname, fname);
    game.packet_save_fp = LbFileOpen(game.packet_fname, Lb_FILE_MODE_READ_ONLY);
    if (game.packet_save_fp == -1)
    {
        ERRORLOG("Cannot open keeper packet file for load");
        game.packet_fopened = 0;
        return false;
    }
    int i = load_game_chunks(game.packet_save_fp, centry);
    if ((i != GLoad_PacketStart) && (i != GLoad_PacketContinue))
    {
        LbFileClose(game.packet_save_fp);
        game.packet_save_fp = -1;
        game.packet_fopened = 0;
        WARNMSG("Couldn't correctly read packet file \"%s\" header.",fname);
        return false;
    }
    game.packet_file_pos = LbFilePosition(game.packet_save_fp);
    game.turns_stored = (LbFileLengthHandle(game.packet_save_fp) - game.packet_file_pos) / PACKET_TURN_SIZE;
    if ((game.packet_checksum_verify) && (!game.packet_save_head.chksum_available))
    {
        WARNMSG("PacketSave checksum not available, checking disabled.");
        game.packet_checksum_verify = false;
    }
    if (game.log_things_start_turn == -1)
    {
        game.log_things_start_turn = 0;
        game.log_things_end_turn = game.turns_stored + 1;
    }
    game.packet_fopened = 1;
    return true;
}

void post_init_packets(void)
{
    SYNCDBG(6,"Starting");
    if ((game.packet_load_enable) && (game.numfield_149F47))
    {
        struct CatalogueEntry centry;
        open_packet_file_for_load(game.packet_fname, &centry);
        game.pckt_gameturn = 0;
    }
    clear_packets();
    packets_first_resync = true;
}

static TbBigChecksum get_thing_simple_checksum(const struct Thing *tng)
{
    return (ulong)tng->mappos.x.val + (ulong)tng->mappos.y.val + (ulong)tng->mappos.z.val
         + (ulong)tng->move_angle_xy + (ulong)tng->owner;
}

static TbBigChecksum get_packet_save_checksum(void)
{
    TbBigChecksum sum = 0;
    for (long tng_idx = 0; tng_idx < THINGS_COUNT; tng_idx++)
    {
        struct Thing* tng = thing_get(tng_idx);
        if ((tng->alloc_flags & TAlF_Exists) != 0)
        {
            // It would be nice to completely ignore effects, but since
            // thing indices are used in packets, lack of effect may cause desync too.
            if ((tng->class_id != TCls_AmbientSnd) && (tng->class_id != TCls_EffectElem))
            {
                sum += get_thing_simple_checksum(tng);
            }
        }
    }
    return sum;
}

short save_packets(void)
{
    const int turn_data_size = PACKET_TURN_SIZE;
    unsigned char pckt_buf[PACKET_TURN_SIZE+4];
    TbBigChecksum chksum;
    SYNCDBG(6,"Starting");
    if (game.packet_checksum_verify)
      chksum = get_packet_save_checksum();
    else
      chksum = 0;
    LbFileSeek(game.packet_save_fp, 0, Lb_FILE_SEEK_END);
    // Prepare data in the buffer
    for (int i = 0; i < NET_PLAYERS_COUNT; i++)
        LbMemoryCopy(&pckt_buf[i*sizeof(struct PacketEx)], &ex_packets[i], sizeof(struct PacketEx));
    LbMemoryCopy(&pckt_buf[NET_PLAYERS_COUNT*sizeof(struct PacketEx)], &chksum, sizeof(TbBigChecksum));
    // Write buffer into file
    if (LbFileWrite(game.packet_save_fp, &pckt_buf, turn_data_size) != turn_data_size)
    {
      ERRORLOG("Packet file write error");
    }
    if ( !LbFileFlush(game.packet_save_fp) )
    {
      ERRORLOG("Unable to flush PacketSave File");
      return false;
    }
    return true;
}

void close_packet_file(void)
{
    if ( game.packet_fopened )
    {
        LbFileClose(game.packet_save_fp);
        game.packet_fopened = 0;
        game.packet_save_fp = -1;
    }
}

void dump_memory_to_file(const char * fname, const char * buf, size_t len)
{
    FILE* file = fopen(fname, "w");
    fwrite(buf, 1, len, file);
    fflush(file);
    fclose(file);
}

void write_debug_packets(void)
{
    //note, changed this to be more general and to handle multiplayer where there can
    //be several players writing to same directory if testing on local machine
    char filename[32];
    snprintf(filename, sizeof(filename), "%s%u.%s", "keeperd", my_player_number, "pck");
    dump_memory_to_file(filename, (char*) ex_packets, sizeof(ex_packets));
}

void write_debug_screenpackets(void)
{
    char filename[32];
    snprintf(filename, sizeof(filename), "%s%u.%s", "keeperd", my_player_number, "spck");
    dump_memory_to_file(filename, (char*) net_screen_packet_NEW, sizeof(net_screen_packet_NEW));
}

TbBool reinit_packets_after_load(void)
{
    game.packet_save_enable = false;
    game.packet_load_enable = false;
    game.packet_save_fp = -1;
    game.packet_fopened = 0;
    return true;
}

TbBool open_new_packet_file_for_save(void)
{
    // Filling the header
    SYNCMSG("Starting packet saving, turn %lu",(unsigned long)game.play_gameturn);
    game.packet_save_head.game_ver_major = VER_MAJOR;
    game.packet_save_head.game_ver_minor = VER_MINOR;
    game.packet_save_head.game_ver_release = VER_RELEASE;
    game.packet_save_head.game_ver_build = VER_BUILD;
    game.packet_save_head.level_num = get_loaded_level_number();
    game.packet_save_head.players_exist = 0;
    game.packet_save_head.players_comp = 0;
    game.packet_save_head.chksum_available = game.packet_checksum_verify;
    for (int i = 0; i < PLAYERS_COUNT; i++)
    {
        struct PlayerInfo* player = get_player(i);
        if (player_exists(player))
        {
            game.packet_save_head.players_exist |= (1 << i) & 0xff;
            if ((player->allocflags & PlaF_CompCtrl) != 0)
              game.packet_save_head.players_comp |= (1 << i) & 0xff;
        }
    }
    LbFileDelete(game.packet_fname);
    game.packet_save_fp = LbFileOpen(game.packet_fname, Lb_FILE_MODE_NEW);
    if (game.packet_save_fp == -1)
    {
        ERRORLOG("Cannot open keeper packet file for save, \"%s\".",game.packet_fname);
        game.packet_fopened = 0;
        return false;
    }
    struct CatalogueEntry centry;
    fill_game_catalogue_entry(&centry, "Packet file");
    if (!save_packet_chunks(game.packet_save_fp,&centry))
    {
        WARNMSG("Cannot write to packet file, \"%s\".",game.packet_fname);
        LbFileClose(game.packet_save_fp);
        game.packet_fopened = 0;
        game.packet_save_fp = -1;
        return false;
    }
    game.packet_fopened = 1;
    return true;
}

void load_packets_for_turn(GameTurn nturn)
{
    SYNCDBG(19,"Starting");
    const int turn_data_size = PACKET_TURN_SIZE;
    unsigned char pckt_buf[PACKET_TURN_SIZE+4]; //TODO: this should not work
    struct PacketEx* pckt = get_packet_ex(my_player_number);
    TbChecksum pckt_chksum = pckt->packet.chksum;
    if (nturn >= game.turns_stored)
    {
        ERRORDBG(18,"Out of turns to load from Packet File");
        erstat_inc(ESE_CantReadPackets);
        return;
    }

    if (LbFileRead(game.packet_save_fp, &pckt_buf, turn_data_size) == -1)
    {
        ERRORDBG(18,"Cannot read turn data from Packet File");
        erstat_inc(ESE_CantReadPackets);
        return;
    }
    game.packet_file_pos += turn_data_size;
    for (long i = 0; i < NET_PLAYERS_COUNT; i++)
        LbMemoryCopy(&ex_packets[i], &pckt_buf[i * sizeof(struct PacketEx)], sizeof(struct PacketEx));
    TbBigChecksum tot_chksum = llong(&pckt_buf[NET_PLAYERS_COUNT * sizeof(struct PacketEx)]);
    if (game.turns_fastforward > 0)
        game.turns_fastforward--;
    if (game.packet_checksum_verify)
    {
        pckt = get_packet_ex(my_player_number);
        if (get_packet_save_checksum() != tot_chksum)
        {
            ERRORLOG("PacketSave checksum - Out of sync (GameTurn %d)", game.play_gameturn);
            if (!is_onscreen_msg_visible())
              show_onscreen_msg(game.num_fps, "Out of sync");
        } else
        if (pckt->packet.chksum != pckt_chksum)
        {
            ERRORLOG("Opps we are really Out Of Sync (GameTurn %d)", game.play_gameturn);
            if (!is_onscreen_msg_visible())
              show_onscreen_msg(game.num_fps, "Out of sync");
        }
    }
}
