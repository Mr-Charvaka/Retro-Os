/*
 * gui/ipc.h - GUI Inter-Process Communication Structures
 */

#ifndef GUI_IPC_H
#define GUI_IPC_H

#include <stdint.h>

#define GUI_IPC_QUEUE_SIZE 64

// Command Opcodes
#define GUI_CMD_NOP 0
#define GUI_CMD_CREATE_WINDOW 1
#define GUI_CMD_DESTROY_WINDOW 2
#define GUI_CMD_SHOW_WINDOW 3
#define GUI_CMD_HIDE_WINDOW 4
#define GUI_CMD_INVALIDATE 5

// IPC Message
typedef struct {
  uint32_t opcode;
  uint8_t payload[124];
} gui_ipc_msg_t;

// Ring Buffer for Messages
typedef struct {
  volatile uint32_t head;
  volatile uint32_t tail;
  gui_ipc_msg_t queue[GUI_IPC_QUEUE_SIZE];
} gui_ipc_ring_t;

#endif // GUI_IPC_H
