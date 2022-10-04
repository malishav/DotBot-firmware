/**
 * @file command.c
 * @addtogroup DRV
 *
 * @brief  nRF52833-specific definition of the "protocol" driver module.
 *
 * @author Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 * @copyright Inria, 2022
 */

#include <stdint.h>
#include <string.h>
#include "device.h"
#include "protocol.h"

//=========================== prototypes =======================================

void db_protocol_header_to_buffer(uint8_t *buffer, uint64_t dst, command_type_t type) {
    uint32_t          src    = db_device_id();
    protocol_header_t header = {
        .dst     = dst,
        .src     = src,
        .version = DB_FIRMWARE_VERSION,
        .type    = type,
    };
    memcpy(buffer, &header, sizeof(protocol_header_t));
}

//=========================== public ===========================================

void db_protocol_cmd_move_raw_to_buffer(uint8_t *buffer, uint64_t dst, protocol_move_raw_command_t *command) {
    db_protocol_header_to_buffer(buffer, dst, DB_PROTOCOL_CMD_MOVE_RAW);
    uint8_t *cmd_ptr = buffer + sizeof(protocol_header_t);
    memcpy(cmd_ptr, command, sizeof(protocol_move_raw_command_t));
}

void db_protocol_cmd_rgbled_to_buffer(uint8_t *buffer, uint64_t dst, protocol_rgbled_command_t *command) {
    db_protocol_header_to_buffer(buffer, dst, DB_PROTOCOL_CMD_RGB_LED);
    uint8_t *cmd_ptr = buffer + sizeof(protocol_header_t);
    memcpy(cmd_ptr, command, sizeof(protocol_rgbled_command_t));
}
