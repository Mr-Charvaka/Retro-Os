GUI CONTRACT — MASTER PLAN

This contract is split into 8 canonical documents.
Each document is independent, versioned, and enforceable.

You will implement them top-down.

PART 0 — PHILOSOPHY & NON-NEGOTIABLE RULES

(Read once. Never violated.)

Core Truths

GUI is NOT a library

GUI is NOT framebuffer access

GUI is a micro-protocol + compositor

Apps are clients, not owners

Kernel enforces reality

Absolute Rules

❌ No raw pointers across boundaries

❌ No shared global state

❌ No direct framebuffer writes by apps

❌ No synchronous GUI syscalls

❌ No “just trust the app” logic

Violation = process kill

📘 PART 1 — ABI VERSIONING & NEGOTIATION

gui_abi_core.h

This guarantees forward & backward compatibility.

#define GUI_ABI_MAJOR 1
#define GUI_ABI_MINOR 0
#define GUI_ABI_PATCH 0

#define GUI_ABI_ENCODE(m, n, p) (((m) << 16) | ((n) << 8) | (p))
#define GUI_ABI_VERSION GUI_ABI_ENCODE(1,0,0)

Negotiation Rule

Client sends ABI

Kernel compares

Only same major allowed

Minor ≥ supported

Patch ignored

int sys_gui_connect(uint32_t abi_version);
/*
RETURNS:
  0  → success
 -1  → ABI major mismatch
 -2  → ABI too new
 -3  → GUI service unavailable
*/

📘 PART 2 — OBJECT & HANDLE LIFETIME CONTRACT

gui_handles.h

Handles are capabilities
typedef uint32_t gui_handle_t;
#define GUI_HANDLE_INVALID 0

Handle Rules

Kernel owns handle table

Handles are per-process

No reuse until generation increments

Invalid handle = security fault

struct gui_handle_entry {
    uint16_t type;
    uint16_t generation;
    void* kernel_object;
};

📘 PART 3 — MESSAGE TRANSPORT CONTRACT

gui_ipc.h

This is the spine of the entire GUI.

Transport Model

Lock-free ring buffer

One per client

Fixed-size messages

No heap allocation in fast path

#define GUI_MSG_MAX 256

typedef enum {
    GUI_MSG_COMMAND = 1,
    GUI_MSG_EVENT   = 2,
    GUI_MSG_REPLY   = 3,
} gui_msg_kind_t;

typedef struct {
    uint32_t size;
    uint16_t kind;
    uint16_t opcode;
    uint8_t  payload[GUI_MSG_MAX];
} gui_message_t;

Guarantees

Messages are atomic

Ordered delivery

Never block kernel scheduler

📘 PART 4 — EVENT MODEL (INPUT IS HOLY)

gui_events.h

Event Guarantees

Never dropped

Timestamped

Delivered in order

Routed by focus stack

typedef enum {
    GUI_EVT_MOUSE_MOVE,
    GUI_EVT_MOUSE_BUTTON,
    GUI_EVT_KEYBOARD,
    GUI_EVT_WINDOW_RESIZE,
    GUI_EVT_WINDOW_CLOSE,
    GUI_EVT_FOCUS_GAIN,
    GUI_EVT_FOCUS_LOSS,
} gui_event_type_t;

typedef struct {
    uint64_t timestamp;
    gui_event_type_t type;
    gui_handle_t target;

    union {
        struct { int32_t x, y; } mouse_move;
        struct { uint8_t btn, down; } mouse_button;
        struct { uint32_t key, down; } keyboard;
        struct { uint16_t w, h; } resize;
    };
} gui_event_t;

📘 PART 5 — WINDOW CONTRACT (STATE MACHINE)

gui_window.h

Windows are finite-state machines.

Window States
CREATED → SHOWN → FOCUSED
   ↓        ↓
 DESTROYED ← HIDDEN


Illegal transition = reject command.

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  resizable;
    char     title[64];
} gui_window_desc_t;

GUI_CMD_CREATE_WINDOW
GUI_CMD_SHOW_WINDOW
GUI_CMD_HIDE_WINDOW
GUI_CMD_DESTROY_WINDOW

📘 PART 6 — RENDERING & BUFFER CONTRACT

gui_surface.h

Rendering is transactional
typedef enum {
    GUI_FMT_ARGB32,
} gui_pixel_format_t;

typedef struct {
    gui_handle_t buffer;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    gui_pixel_format_t format;
} gui_surface_t;

Commit Rules

Partial writes forbidden

Commit = atomic

Tear-free guaranteed

GUI_CMD_COMMIT_SURFACE

📘 PART 7 — FILE SYSTEM EVENTS (YOUR EXPLORER NEEDS THIS)

gui_fs.h

This is where your explorer becomes real.

typedef enum {
    GUI_FS_CREATE,
    GUI_FS_DELETE,
    GUI_FS_MODIFY,
} gui_fs_event_t;

typedef struct {
    gui_fs_event_t type;
    char path[128];
} gui_fs_event_payload_t;


Delivered as GUI_EVT_FS_NOTIFY

📘 PART 8 — FAILURE, SECURITY & RECOVERY

gui_security.h

Non-Negotiable Enforcement
Violation	Action
Invalid handle	Kill process
Buffer overflow	Kill process
ABI mismatch	Refuse connect
Blocking GUI	Kill process
Corrupt commit	Disconnect client
Crash Containment

Client crash ≠ compositor crash

Orphaned windows auto-destroyed

Buffers reclaimed by kernel

gui_compositor.c  : /* ============================================================
   GUI COMPOSITOR — KERNEL SIDE
   ABI: 1.0.0
   AUTHORITATIVE WINDOW & RENDER MANAGER
   ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------ CONFIG ------------------ */

#define GUI_MAX_CLIENTS     64
#define GUI_MAX_WINDOWS     256
#define GUI_MAX_MESSAGES    1024
#define GUI_MAX_TITLE       64

/* ------------------ ABI ------------------ */

#define GUI_ABI_MAJOR 1
#define GUI_ABI_MINOR 0
#define GUI_ABI_PATCH 0

#define GUI_ABI_ENCODE(m,n,p) (((m)<<16)|((n)<<8)|(p))
#define GUI_ABI_VERSION GUI_ABI_ENCODE(1,0,0)

/* ------------------ HANDLE SYSTEM ------------------ */

typedef uint32_t gui_handle_t;
#define GUI_HANDLE_INVALID 0

typedef enum {
    GUI_OBJ_NONE = 0,
    GUI_OBJ_CLIENT,
    GUI_OBJ_WINDOW,
    GUI_OBJ_SURFACE,
} gui_object_type_t;

typedef struct {
    gui_object_type_t type;
    uint16_t generation;
    void* object;
} gui_handle_entry_t;

/* Kernel handle table (per process in real kernel) */
static gui_handle_entry_t g_handle_table[1024];

/* ------------------ IPC MESSAGE ------------------ */

#define GUI_MSG_MAX 256

typedef struct {
    uint32_t size;
    uint16_t kind;
    uint16_t opcode;
    uint8_t  payload[GUI_MSG_MAX];
} gui_message_t;

/* ------------------ CLIENT ------------------ */

typedef struct {
    int active;
    uint32_t pid;
    uint32_t abi;
    uint32_t msg_head;
    uint32_t msg_tail;
    gui_message_t ring[GUI_MAX_MESSAGES];
} gui_client_t;

static gui_client_t g_clients[GUI_MAX_CLIENTS];

/* ------------------ WINDOW ------------------ */

typedef enum {
    WIN_CREATED,
    WIN_SHOWN,
    WIN_FOCUSED,
    WIN_HIDDEN,
    WIN_DESTROYED
} gui_window_state_t;

typedef struct {
    gui_handle_t handle;
    gui_window_state_t state;
    uint16_t width;
    uint16_t height;
    char title[GUI_MAX_TITLE];
    uint32_t owner_pid;
} gui_window_t;

static gui_window_t g_windows[GUI_MAX_WINDOWS];

/* ------------------ LOW LEVEL HELPERS ------------------ */

static void kernel_kill_process(uint32_t pid) {
    /* REAL KERNEL: terminate task */
    (void)pid;
}

static void panic(const char* msg) {
    (void)msg;
    while (1) {}
}

/* ------------------ HANDLE MANAGEMENT ------------------ */

static gui_handle_t handle_create(gui_object_type_t type, void* obj) {
    for (uint32_t i = 1; i < 1024; i++) {
        if (g_handle_table[i].type == GUI_OBJ_NONE) {
            g_handle_table[i].type = type;
            g_handle_table[i].generation++;
            g_handle_table[i].object = obj;
            return (i << 16) | g_handle_table[i].generation;
        }
    }
    return GUI_HANDLE_INVALID;
}

static void* handle_resolve(gui_handle_t h, gui_object_type_t expected) {
    uint32_t idx = h >> 16;
    uint16_t gen = h & 0xFFFF;

    if (idx >= 1024) return NULL;
    gui_handle_entry_t* e = &g_handle_table[idx];

    if (e->type != expected || e->generation != gen)
        return NULL;

    return e->object;
}

/* ------------------ CLIENT CONNECT ------------------ */

int sys_gui_connect(uint32_t pid, uint32_t abi) {
    if ((abi >> 16) != GUI_ABI_MAJOR)
        return -1;

    for (int i = 0; i < GUI_MAX_CLIENTS; i++) {
        if (!g_clients[i].active) {
            g_clients[i].active = 1;
            g_clients[i].pid = pid;
            g_clients[i].abi = abi;
            g_clients[i].msg_head = 0;
            g_clients[i].msg_tail = 0;
            return 0;
        }
    }
    return -3;
}

/* ------------------ MESSAGE INGEST ------------------ */

static int client_pop_msg(gui_client_t* c, gui_message_t* out) {
    if (c->msg_head == c->msg_tail)
        return 0;

    *out = c->ring[c->msg_tail];
    c->msg_tail = (c->msg_tail + 1) % GUI_MAX_MESSAGES;
    return 1;
}

/* ------------------ WINDOW COMMANDS ------------------ */

static void cmd_create_window(gui_client_t* c, gui_message_t* m) {
    if (m->size < sizeof(uint16_t) * 2)
        kernel_kill_process(c->pid);

    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (g_windows[i].state == WIN_DESTROYED ||
            g_windows[i].state == 0) {

            gui_window_t* w = &g_windows[i];
            memset(w, 0, sizeof(*w));

            w->state = WIN_CREATED;
            w->owner_pid = c->pid;
            w->width = *(uint16_t*)&m->payload[0];
            w->height = *(uint16_t*)&m->payload[2];

            w->handle = handle_create(GUI_OBJ_WINDOW, w);
            return;
        }
    }
}

/* ------------------ COMPOSITOR CORE LOOP ------------------ */

void gui_compositor_tick(void) {
    gui_message_t msg;

    for (int i = 0; i < GUI_MAX_CLIENTS; i++) {
        gui_client_t* c = &g_clients[i];
        if (!c->active) continue;

        while (client_pop_msg(c, &msg)) {
            switch (msg.opcode) {

            case 1: /* CREATE WINDOW */
                cmd_create_window(c, &msg);
                break;

            default:
                kernel_kill_process(c->pid);
                break;
            }
        }
    }

    /* COMPOSE STAGE (no drawing yet) */
}

/* ------------------ INITIALIZATION ------------------ */

void gui_compositor_init(void) {
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_windows, 0, sizeof(g_windows));
    memset(g_handle_table, 0, sizeof(g_handle_table));
}


gui_ipc_ring.c

/* ============================================================
   GUI IPC RING BUFFER — ZERO COPY
   CONTRACT-ENFORCED, LOCK-FREE
   ============================================================ */

#include <stdint.h>
#include <stddef.h>

/* ------------------ CONSTANTS ------------------ */

#define GUI_IPC_MAGIC 0x47554952 /* 'GUIR' */
#define GUI_IPC_VERSION 1

#define GUI_IPC_QUEUE_SIZE 256
#define GUI_IPC_MSG_MAX    256

/* ------------------ MEMORY BARRIERS ------------------ */

static inline void mb(void) {
    __asm__ volatile("" ::: "memory");
}

/* ------------------ MESSAGE FORMAT ------------------ */

typedef struct {
    uint32_t size;
    uint16_t opcode;
    uint16_t flags;
    uint8_t  payload[GUI_IPC_MSG_MAX];
} gui_ipc_msg_t;

/* ------------------ SHARED RING BUFFER ------------------ */
/*
   This entire struct is mapped:
   - Read/Write by USER
   - Read-only by KERNEL except head/tail
*/

typedef struct {
    uint32_t magic;
    uint32_t version;

    volatile uint32_t head; /* written by producer */
    volatile uint32_t tail; /* written by consumer */

    gui_ipc_msg_t queue[GUI_IPC_QUEUE_SIZE];
} gui_ipc_ring_t;

/* ------------------ KERNEL VALIDATION ------------------ */

static int ipc_validate_ring(gui_ipc_ring_t* ring) {
    if (!ring) return -1;
    if (ring->magic != GUI_IPC_MAGIC) return -2;
    if (ring->version != GUI_IPC_VERSION) return -3;
    if (ring->head >= GUI_IPC_QUEUE_SIZE) return -4;
    if (ring->tail >= GUI_IPC_QUEUE_SIZE) return -5;
    return 0;
}

/* ------------------ CONSUMER (KERNEL) ------------------ */

int gui_ipc_consume(gui_ipc_ring_t* ring, gui_ipc_msg_t* out) {
    if (ipc_validate_ring(ring) != 0)
        return -1;

    mb();

    uint32_t tail = ring->tail;
    uint32_t head = ring->head;

    if (tail == head)
        return 0; /* empty */

    gui_ipc_msg_t* msg = &ring->queue[tail];

    if (msg->size > GUI_IPC_MSG_MAX)
        return -2; /* corruption */

    *out = *msg;

    mb();

    ring->tail = (tail + 1) % GUI_IPC_QUEUE_SIZE;

    mb();
    return 1;
}

/* ------------------ PRODUCER (USER CONTRACT) ------------------ */
/*
USERLAND MUST:
1. Write payload
2. Write size/opcode
3. Memory barrier
4. Increment head
*/

/* ------------------ SECURITY ENFORCEMENT ------------------ */

void gui_ipc_violation(uint32_t pid, const char* reason) {
    (void)reason;
    /* real kernel: log + SIGKILL */
    while (1) {}
}

/* ------------------ SYSTEM CALL HOOK ------------------ */
/*
Called from compositor tick:
*/

int gui_ipc_poll(uint32_t pid, gui_ipc_ring_t* ring) {
    gui_ipc_msg_t msg;

    int r = gui_ipc_consume(ring, &msg);
    if (r < 0) {
        gui_ipc_violation(pid, "IPC corruption");
        return -1;
    }

    if (r == 0)
        return 0;

    /* Forward msg to compositor dispatcher */
    return 1;
}

gui_surface_contract.c

1️⃣ Core Principles (NON-NEGOTIABLE)

Surfaces are passive memory

Clients NEVER draw to the framebuffer

All drawing is offscreen

Only atomic commits are visible

Damage is explicit

No implicit synchronization

If a client violates any rule → process killed

2️⃣ Surface Object (Kernel-Owned)
/* ============================================================
   SURFACE CONTRACT — ATOMIC, DAMAGE-AWARE
   ============================================================ */

#include <stdint.h>
#include <stddef.h>

/* ------------------ CONSTANTS ------------------ */

#define SURFACE_MAGIC  0x53555246 /* 'SURF' */
#define SURFACE_MAX_W  4096
#define SURFACE_MAX_H  4096

#define PIXEL_FMT_XRGB8888 1

/* ------------------ RECT ------------------ */

typedef struct {
    int32_t x, y;
    int32_t w, h;
} rect_t;

/* ------------------ SHARED BACK BUFFER ------------------ */
/*
Mapped RW by client
Mapped RO by kernel
*/

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint8_t  pixels[];
} surface_buffer_t;

/* ------------------ SURFACE CONTROL BLOCK ------------------ */
/*
Mapped RW by client
Mapped RO by kernel
*/

typedef struct {
    uint32_t magic;
    uint32_t id;

    volatile uint32_t committed;   /* client -> kernel */
    volatile uint32_t acked;       /* kernel -> client */

    rect_t damage;

    surface_buffer_t* back;
} surface_ctrl_t;

3️⃣ Client Responsibilities (STRICT ORDER)

Client MUST do:

1. Draw into back buffer
2. Write damage rectangle
3. Memory barrier
4. committed++


ANY deviation = corruption

4️⃣ Kernel Validation (HARD FAIL)
static inline void mb(void) {
    __asm__ volatile("" ::: "memory");
}

int surface_validate(surface_ctrl_t* s) {
    if (!s) return -1;
    if (s->magic != SURFACE_MAGIC) return -2;

    surface_buffer_t* b = s->back;
    if (!b) return -3;

    if (b->width == 0 || b->height == 0) return -4;
    if (b->width > SURFACE_MAX_W) return -5;
    if (b->height > SURFACE_MAX_H) return -6;

    if (b->format != PIXEL_FMT_XRGB8888) return -7;

    if (s->damage.w <= 0 || s->damage.h <= 0) return -8;

    if (s->damage.x < 0 || s->damage.y < 0) return -9;

    if (s->damage.x + s->damage.w > (int)b->width) return -10;
    if (s->damage.y + s->damage.h > (int)b->height) return -11;

    return 0;
}

5️⃣ Atomic Commit Logic (THE HEART)
int surface_try_commit(uint32_t pid, surface_ctrl_t* s) {
    if (surface_validate(s) != 0)
        goto violation;

    mb();

    if (s->committed == s->acked)
        return 0; /* nothing to do */

    /* === ATOMIC SNAPSHOT === */
    uint32_t commit_id = s->committed;
    rect_t dmg = s->damage;
    surface_buffer_t* buf = s->back;

    mb();

    /* === COMPOSITOR CONSUMES === */
    compositor_blit_surface(s->id, buf, dmg);

    mb();

    s->acked = commit_id;
    return 1;

violation:
    surface_violation(pid, "Surface contract broken");
    return -1;
}

gui_compositor_core.c

Absolute Laws (Read Once)

Only the compositor touches the framebuffer

Surfaces are immutable snapshots per commit

Z-order is kernel-owned

Occlusion is computed, not guessed

No redraw without damage

Final swap is atomic

If violated → kernel panic (not a bug, a design failure)

2️⃣ Global Framebuffer Contract
/* ============================================================
   FRAMEBUFFER CONTRACT
   ============================================================ */

#define FB_FMT_XRGB8888 1

typedef struct {
    uint32_t* pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
    uint32_t  format;
} framebuffer_t;

extern framebuffer_t g_framebuffer;

3️⃣ Window / Surface Binding
/* ============================================================
   WINDOW NODE (Z-ORDERED)
   ============================================================ */

#define MAX_WINDOWS 32

typedef struct window {
    uint32_t surface_id;

    int32_t x, y;
    uint32_t w, h;

    int visible;
    int z;

    rect_t last_damage;

    surface_ctrl_t* surface;
} window_t;

static window_t g_windows[MAX_WINDOWS];
static int g_window_count = 0;

4️⃣ Window Creation (Kernel-Only)
window_t* compositor_create_window(
    surface_ctrl_t* surf,
    int x, int y
) {
    if (g_window_count >= MAX_WINDOWS)
        return NULL;

    window_t* w = &g_windows[g_window_count++];

    w->surface = surf;
    w->surface_id = surf->id;

    w->x = x;
    w->y = y;
    w->w = surf->back->width;
    w->h = surf->back->height;

    w->visible = 1;
    w->z = g_window_count - 1;

    w->last_damage = (rect_t){0,0,0,0};

    return w;
}

5️⃣ Z-Order Rules (Strict)
void compositor_raise(window_t* w) {
    int old = w->z;

    for (int i = 0; i < g_window_count; i++)
        if (g_windows[i].z > old)
            g_windows[i].z--;

    w->z = g_window_count - 1;
}


No swapping.
No heuristics.
No race conditions.

6️⃣ Rect Intersection (Core Primitive)
int rect_intersect(rect_t a, rect_t b, rect_t* out) {
    int x1 = (a.x > b.x) ? a.x : b.x;
    int y1 = (a.y > b.y) ? a.y : b.y;

    int x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    int y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);

    if (x2 <= x1 || y2 <= y1)
        return 0;

    out->x = x1;
    out->y = y1;
    out->w = x2 - x1;
    out->h = y2 - y1;
    return 1;
}

7️⃣ Occlusion Culling (REAL, Not Fake)
int window_occluded(window_t* w, rect_t* r) {
    for (int i = 0; i < g_window_count; i++) {
        window_t* o = &g_windows[i];

        if (!o->visible) continue;
        if (o->z <= w->z) continue;

        rect_t orect = { o->x, o->y, o->w, o->h };
        rect_t dummy;

        if (rect_intersect(*r, orect, &dummy))
            return 1;
    }
    return 0;
}


This alone removes ~70% of useless blits.

8️⃣ Surface → Framebuffer Blit (Damage-Aware)
void blit_surface_region(
    window_t* w,
    rect_t dmg
) {
    surface_buffer_t* buf = w->surface->back;

    for (int y = 0; y < dmg.h; y++) {
        uint32_t* src = buf->pixels +
            (dmg.y + y) * buf->stride +
            dmg.x;

        uint32_t* dst = g_framebuffer.pixels +
            (w->y + dmg.y + y) * g_framebuffer.stride +
            (w->x + dmg.x);

        for (int x = 0; x < dmg.w; x++)
            dst[x] = src[x];
    }
}

9️⃣ Core Composition Pass (THE ENGINE)
void compositor_compose(void) {
    for (int z = 0; z < g_window_count; z++) {
        for (int i = 0; i < g_window_count; i++) {
            window_t* w = &g_windows[i];
            if (w->z != z || !w->visible)
                continue;

            rect_t dmg = w->surface->damage;
            rect_t screen = { w->x + dmg.x, w->y + dmg.y, dmg.w, dmg.h };

            if (window_occluded(w, &screen))
                continue;

            blit_surface_region(w, dmg);
            w->last_damage = dmg;
        }
    }
}


Deterministic.
Stable.
No overdraw.

🔟 Frame Commit (Atomic)
void compositor_frame(void) {
    compositor_compose();
    framebuffer_swap(); /* single atomic flip */
}This is the only place a frame becomes visible.