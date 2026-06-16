#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include "raylib.h"

static void RRL(Rectangle r, float rnd, int seg, Color col) {
    DrawRectangleRounded(r, rnd, seg, (Color){0, 0, 0, 0});
    DrawRectangleLinesEx(r, 1.5f, col);
}

#define MAX_PROC 12
#define MAX_GANTT 4096
#define SW 1280
#define SH 800
#define TAB_H 52

static Color C_BG      = {8, 8, 24, 255};
static Color C_PANEL   = {18, 12, 40, 255};
static Color C_VIOLET  = {150, 30, 230, 255};
static Color C_LBLUE   = {0, 210, 255, 255};
static Color C_DIMV    = {40, 10, 60, 255};
static Color C_DIMB    = {0, 35, 55, 255};
static Color C_ACCENT  = {200, 80, 255, 255};
static Color C_WHITE   = {230, 230, 255, 255};
static Color C_GRAY    = {80, 70, 110, 255};
static Color C_RED     = {255, 70, 70, 255};
static Color C_GREEN   = {60, 220, 120, 255};
static Color C_ORANGE  = {255, 160, 40, 255};

static Color PCOLS[12] = {
    {0, 210, 255, 255},   {180, 40, 255, 255},  {255, 80, 160, 255},  {60, 220, 120, 255},
    {255, 160, 40, 255},  {120, 200, 255, 255}, {220, 100, 255, 255}, {255, 220, 60, 255},
    {80, 255, 180, 255},  {255, 120, 80, 255},  {100, 160, 255, 100}, {200, 255, 100, 255}
};

static volatile int alarm_fired = 0;
static int timer_total = 0, timer_remaining = 0, timer_running = 0, timer_done = 0;
static float flash_t = 0;

void handle_alarm(int sig) {
    (void)sig;
    alarm_fired = 1;
}

typedef struct {
    int x, y, w, h;
} SR;

void dseg(SR s, Color c) {
    DrawRectangle(s.x, s.y, s.w, s.h, c);
}

void ddigit(int x, int y, int sw, int sh, int g, int d, Color on, Color off) {
    int hs = sh / 2;
    SR top = {x + g, y, sw - 2 * g, g}, mid = {x + g, y + hs, sw - 2 * g, g}, bot = {x + g, y + sh - g, sw - 2 * g, g};
    SR tl = {x, y + g, g, hs - g}, tr = {x + sw - g, y + g, g, hs - g};
    SR bl = {x, y + hs + g, g, hs - 2 * g}, br = {x + sw - g, y + hs + g, g, hs - 2 * g};
    
    int s7[10][7] = {
        {1, 0, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 1, 0, 1}, {1, 1, 1, 0, 1, 1, 0}, {1, 1, 1, 0, 1, 0, 1},
        {0, 1, 0, 1, 1, 0, 1}, {1, 1, 1, 1, 0, 0, 1}, {1, 1, 1, 1, 0, 1, 1}, {1, 0, 0, 0, 1, 0, 1},
        {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 0, 1}
    };
    
    if (d < 0 || d > 9) return;
    
    dseg(top, s7[d][0] ? on : off);
    dseg(mid, s7[d][1] ? on : off);
    dseg(bot, s7[d][2] ? on : off);
    dseg(tl, s7[d][3] ? on : off);
    dseg(tr, s7[d][4] ? on : off);
    dseg(bl, s7[d][5] ? on : off);
    dseg(br, s7[d][6] ? on : off);
}

void dcolon(int x, int y, int h, Color c) {
    int r = h / 12;
    DrawCircle(x, y + h / 3, r, c);
    DrawCircle(x, y + 2 * h / 3, r, c);
}

void draw_clock(int secs, int ox, int oy, int dw, int dh, int g, Color on, Color off, Color cc) {
    int sp = dw + g * 2;
    ddigit(ox, oy, dw, dh, g, (secs / 60) / 10, on, off);
    ddigit(ox + sp, oy, dw, dh, g, (secs / 60) % 10, on, off);
    dcolon(ox + sp * 2 - g, oy, dh, cc);
    ddigit(ox + sp * 2, oy, dw, dh, g, (secs % 60) / 10, on, off);
    ddigit(ox + sp * 3, oy, dw, dh, g, (secs % 60) % 10, on, off);
}

typedef struct {
    char name[8];
    int arrival, burst, priority;
    int remaining, finish, waiting, turnaround;
    Color col;
} Proc;

typedef struct {
    int pid;
    int start, end;
    int is_interrupt;
} GSlot;

static Proc procs[MAX_PROC];
static int nproc = 0;
static GSlot sched_full[MAX_GANTT];
static int sched_n = 0, sched_total_time = 0;
static float avg_wait = 0, avg_tat = 0;

static int anim_running = 0, anim_done = 0;
static float anim_time = 0, anim_speed = 1.0f;
static int rr_quantum = 2;

static int sjf_preemptive = 0;
static int prio_preemptive = 0;

typedef enum { FCFS = 0, SJF = 1, RR = 2, PRIO = 3 } Algo;

void add_gs_idle(int s, int e) {
    if (sched_n >= MAX_GANTT) return;
    sched_full[sched_n++] = (GSlot){-1, s, e, 0};
}

void add_gs(int pid, int s, int e, int intr) {
    if (!intr && sched_n > 0 && sched_full[sched_n - 1].pid == pid && !sched_full[sched_n - 1].is_interrupt) {
        sched_full[sched_n - 1].end = e;
        return;
    }
    if (sched_n >= MAX_GANTT) return;
    sched_full[sched_n++] = (GSlot){pid, s, e, intr};
}

void build_fcfs(void) {
    int idx[MAX_PROC];
    for (int i = 0; i < nproc; i++) idx[i] = i;
    for (int i = 0; i < nproc - 1; i++) {
        for (int j = i + 1; j < nproc; j++) {
            if (procs[idx[i]].arrival > procs[idx[j]].arrival) {
                int x = idx[i];
                idx[i] = idx[j];
                idx[j] = x;
            }
        }
    }
    int t = 0;
    for (int ii = 0; ii < nproc; ii++) {
        int i = idx[ii];
        if (t < procs[i].arrival) {
            add_gs_idle(t, procs[i].arrival);
            t = procs[i].arrival;
        }
        add_gs(i, t, t + procs[i].burst, 0);
        t += procs[i].burst;
        procs[i].finish = t;
        procs[i].turnaround = t - procs[i].arrival;
        procs[i].waiting = procs[i].turnaround - procs[i].burst;
    }
}

void build_sjf_np(void) {
    int done[MAX_PROC] = {0};
    int t = 0, fin = 0;
    while (fin < nproc) {
        int best = -1;
        for (int i = 0; i < nproc; i++) {
            if (!done[i] && procs[i].arrival <= t && (best == -1 || procs[i].burst < procs[best].burst)) {
                best = i;
            }
        }
        if (best == -1) {
            int nt = 1 << 30;
            for (int i = 0; i < nproc; i++) {
                if (!done[i] && procs[i].arrival < nt) nt = procs[i].arrival;
            }
            add_gs_idle(t, nt);
            t = nt;
            continue;
        }
        add_gs(best, t, t + procs[best].burst, 0);
        t += procs[best].burst;
        procs[best].finish = t;
        procs[best].turnaround = t - procs[best].arrival;
        procs[best].waiting = procs[best].turnaround - procs[best].burst;
        done[best] = 1;
        fin++;
    }
}

void build_sjf_p(void) {
    int rem[MAX_PROC];
    for (int i = 0; i < nproc; i++) rem[i] = procs[i].burst;
    int done[MAX_PROC] = {0};
    int t = 0, fin = 0, prev = -1;
    while (fin < nproc) {
        int best = -1;
        for (int i = 0; i < nproc; i++) {
            if (!done[i] && procs[i].arrival <= t && (best == -1 || rem[i] < rem[best])) {
                best = i;
            }
        }
        if (best == -1) {
            int nt = 1 << 30;
            for (int i = 0; i < nproc; i++) {
                if (!done[i] && procs[i].arrival < nt) nt = procs[i].arrival;
            }
            add_gs_idle(t, nt);
            t = nt;
            prev = -1;
            continue;
        }
        if (prev != -1 && prev != best) {
            if (sched_n > 0) sched_full[sched_n - 1].is_interrupt = 1;
        }
        if (sched_n == 0 || sched_full[sched_n - 1].pid != best) {
            if (sched_n >= MAX_GANTT) break;
            sched_full[sched_n++] = (GSlot){best, t, t + 1, 0};
        } else {
            sched_full[sched_n - 1].end = t + 1;
        }
        rem[best]--;
        t++;
        prev = best;
        if (rem[best] == 0) {
            procs[best].finish = t;
            procs[best].turnaround = t - procs[best].arrival;
            procs[best].waiting = procs[best].turnaround - procs[best].burst;
            done[best] = 1;
            fin++;
            prev = -1;
        }
    }
}

void build_rr(int q) {
    int rem[MAX_PROC];
    for (int i = 0; i < nproc; i++) rem[i] = procs[i].burst;
    int ord[MAX_PROC];
    for (int i = 0; i < nproc; i++) ord[i] = i;
    for (int i = 0; i < nproc - 1; i++) {
        for (int j = i + 1; j < nproc; j++) {
            if (procs[ord[i]].arrival > procs[ord[j]].arrival) {
                int x = ord[i];
                ord[i] = ord[j];
                ord[j] = x;
            }
        }
    }
    int queue[MAX_GANTT * 4], qh = 0, qt = 0;
    int inq[MAX_PROC] = {0};
    int t = 0, fin = 0;
    for (int i = 0; i < nproc; i++) {
        if (procs[ord[i]].arrival == 0) {
            queue[qt++] = ord[i];
            inq[ord[i]] = 1;
        }
    }
    while (fin < nproc) {
        if (qh == qt) {
            int nt = 1 << 30;
            for (int i = 0; i < nproc; i++) {
                if (!inq[i] && rem[i] > 0 && procs[i].arrival < nt) nt = procs[i].arrival;
            }
            if (nt == 1 << 30) break;
            add_gs_idle(t, nt);
            t = nt;
            for (int i = 0; i < nproc; i++) {
                if (!inq[i] && rem[i] > 0 && procs[i].arrival <= t) {
                    queue[qt++] = i;
                    inq[i] = 1;
                }
            }
            continue;
        }
        int cur = queue[qh++];
        int run = rem[cur] < q ? rem[cur] : q;
        int preempted = (rem[cur] > q);
        add_gs(cur, t, t + run, preempted);
        t += run;
        rem[cur] -= run;
        for (int i = 0; i < nproc; i++) {
            if (!inq[i] && rem[i] > 0 && procs[i].arrival <= t) {
                queue[qt++] = i;
                inq[i] = 1;
            }
        }
        if (rem[cur] == 0) {
            procs[cur].finish = t;
            procs[cur].turnaround = t - procs[cur].arrival;
            procs[cur].waiting = procs[cur].turnaround - procs[cur].burst;
            fin++;
        } else {
            queue[qt++] = cur;
        }
    }
}

void build_prio_np(void) {
    int done[MAX_PROC] = {0};
    int t = 0, fin = 0;
    while (fin < nproc) {
        int best = -1;
        for (int i = 0; i < nproc; i++) {
            if (!done[i] && procs[i].arrival <= t && (best == -1 || procs[i].priority < procs[best].priority)) {
                best = i;
            }
        }
        if (best == -1) {
            int nt = 1 << 30;
            for (int i = 0; i < nproc; i++) {
                if (!done[i] && procs[i].arrival < nt) nt = procs[i].arrival;
            }
            add_gs_idle(t, nt);
            t = nt;
            continue;
        }
        add_gs(best, t, t + procs[best].burst, 0);
        t += procs[best].burst;
        procs[best].finish = t;
        procs[best].turnaround = t - procs[best].arrival;
        procs[best].waiting = procs[best].turnaround - procs[best].burst;
        done[best] = 1;
        fin++;
    }
}

void build_prio_p(void) {
    int rem[MAX_PROC];
    for (int i = 0; i < nproc; i++) rem[i] = procs[i].burst;
    int done[MAX_PROC] = {0};
    int t = 0, fin = 0, prev = -1;
    while (fin < nproc) {
        int best = -1;
        for (int i = 0; i < nproc; i++) {
            if (!done[i] && procs[i].arrival <= t && (best == -1 || procs[i].priority < procs[best].priority)) {
                best = i;
            }
        }
        if (best == -1) {
            if (prev != -1 && sched_n > 0) sched_full[sched_n - 1].is_interrupt = 0;
            int nt = 1 << 30;
            for (int i = 0; i < nproc; i++) {
                if (!done[i] && procs[i].arrival < nt) nt = procs[i].arrival;
            }
            add_gs_idle(t, nt);
            t = nt;
            prev = -1;
            continue;
        }
        if (prev != -1 && prev != best && sched_n > 0) {
            sched_full[sched_n - 1].is_interrupt = 1;
        }
        if (sched_n == 0 || sched_full[sched_n - 1].pid != best) {
            if (sched_n >= MAX_GANTT) break;
            sched_full[sched_n++] = (GSlot){best, t, t + 1, 0};
        } else {
            sched_full[sched_n - 1].end = t + 1;
        }
        rem[best]--;
        t++;
        prev = best;
        if (rem[best] == 0) {
            procs[best].finish = t;
            procs[best].turnaround = t - procs[best].arrival;
            procs[best].waiting = procs[best].turnaround - procs[best].burst;
            done[best] = 1;
            fin++;
            prev = -1;
        }
    }
}

void run_schedule(Algo algo) {
    for (int i = 0; i < nproc; i++) {
        procs[i].remaining = procs[i].burst;
        procs[i].finish = procs[i].waiting = procs[i].turnaround = 0;
    }
    sched_n = 0;
    avg_wait = 0;
    avg_tat = 0;
    switch (algo) {
        case FCFS: build_fcfs(); break;
        case SJF:  sjf_preemptive ? build_sjf_p() : build_sjf_np(); break;
        case RR:   build_rr(rr_quantum); break;
        case PRIO: prio_preemptive ? build_prio_p() : build_prio_np(); break;
    }
    float sw = 0, st = 0;
    for (int i = 0; i < nproc; i++) {
        sw += procs[i].waiting;
        st += procs[i].turnaround;
    }
    avg_wait = sw / nproc;
    avg_tat = st / nproc;
    sched_total_time = sched_n ? sched_full[sched_n - 1].end : 0;
    anim_time = 0;
    anim_running = 0;
    anim_done = 0;
}

typedef struct {
    char buf[32];
    int len;
    int active;
} Field;

static Field fld[MAX_PROC][4];
static Field timer_fld, quantum_fld;

int fint(Field* f) {
    return f->len ? atoi(f->buf) : 0;
}

void field_draw(Field* f, Rectangle r, const char* ph) {
    DrawRectangleRounded(r, 0.2f, 4, (Color){12, 8, 28, 255});
    RRL(r, 0.2f, 4, f->active ? C_LBLUE : C_DIMV);
    if (!f->len && !f->active) {
        int tw = MeasureText(ph, 13);
        DrawText(ph, (int)(r.x + (r.width - tw) / 2), (int)(r.y + 5), 13, C_GRAY);
    } else {
        DrawText(f->buf, (int)(r.x + 5), (int)(r.y + 5), 13, C_WHITE);
        if (f->active && (int)(GetTime() * 2) % 2 == 0) {
            DrawText("|", (int)(r.x + 5 + MeasureText(f->buf, 13)), (int)(r.y + 5), 13, C_LBLUE);
        }
    }
}

void field_update(Field* f, Rectangle r, int alpha) {
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        Vector2 m = GetMousePosition();
        f->active = CheckCollisionPointRec(m, r);
    }
    if (!f->active) return;
    int k = GetCharPressed();
    while (k > 0) {
        if ((alpha || (k >= '0' && k <= '9')) && f->len < (int)sizeof(f->buf) - 2) {
            f->buf[f->len++] = (char)k;
            f->buf[f->len] = 0;
        }
        k = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && f->len > 0) {
        f->buf[--f->len] = 0;
    }
}

int hov(Rectangle r) {
    Vector2 m = GetMousePosition();
    return CheckCollisionPointRec(m, r);
}

int clicked(Rectangle r) {
    return hov(r) && IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
}

void draw_panel(Rectangle r, float rnd) {
    DrawRectangleRounded(r, rnd, 8, C_PANEL);
    RRL(r, rnd, 8, C_DIMV);
}

void draw_btn(Rectangle r, const char* lbl, int active, int h) {
    Color bg = active ? (Color){100, 20, 180, 255} : h ? (Color){40, 15, 70, 255} : C_DIMV;
    Color bdr = active ? C_ACCENT : h ? C_VIOLET : C_GRAY;
    Color tc = active ? C_WHITE : h ? C_LBLUE : C_GRAY;
    DrawRectangleRounded(r, 0.3f, 6, bg);
    RRL(r, 0.3f, 6, bdr);
    int tw = MeasureText(lbl, 14);
    DrawText(lbl, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 14) / 2), 14, tc);
}

void draw_toggle(Rectangle r, const char* lbl, int on) {
    Color bg = on ? (Color){20, 80, 20, 255} : (Color){60, 10, 10, 255};
    Color bdr = on ? C_GREEN : C_RED;
    Color tc = on ? C_GREEN : C_RED;
    DrawRectangleRounded(r, 0.4f, 6, bg);
    RRL(r, 0.4f, 6, bdr);
    int fs = 11;
    int tw = MeasureText(lbl, fs);
    if (tw > (int)r.width - 4) {
        fs = 9;
        tw = MeasureText(lbl, fs);
    }
    DrawText(lbl, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - fs) / 2), fs, tc);
}

typedef enum { TAB_TIMER = 0, TAB_SCHED = 1 } Tab;

static float scroll_y = 0.0f;
#define VY(y) ((int)((y) - scroll_y + TAB_H))

int main(void) {
    signal(SIGALRM, handle_alarm);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SW, SH, "Timer + CPU Scheduler");
    SetTargetFPS(60);

    Tab tab = TAB_TIMER;
    Algo algo = FCFS;
    int sched_ran = 0;

    memset(fld, 0, sizeof(fld));
    memset(&timer_fld, 0, sizeof(timer_fld));
    memset(&quantum_fld, 0, sizeof(quantum_fld));
    strcpy(quantum_fld.buf, "2");
    quantum_fld.len = 1;

    nproc = 4;
    const char* dn[] = {"P1", "P2", "P3", "P4"};
    int da[] = {0, 2, 4, 6}, db[] = {5, 3, 1, 4}, dp[] = {2, 1, 3, 1};
    for (int i = 0; i < nproc; i++) {
        strcpy(procs[i].name, dn[i]);
        procs[i].arrival = da[i];
        procs[i].burst = db[i];
        procs[i].priority = dp[i];
        procs[i].remaining = db[i];
        procs[i].col = PCOLS[i];
        sprintf(fld[i][3].buf, "%s", dn[i]);
        fld[i][3].len = strlen(fld[i][3].buf);
        sprintf(fld[i][0].buf, "%d", da[i]);
        fld[i][0].len = strlen(fld[i][0].buf);
        sprintf(fld[i][1].buf, "%d", db[i]);
        fld[i][1].len = strlen(fld[i][1].buf);
        sprintf(fld[i][2].buf, "%d", dp[i]);
        fld[i][2].len = strlen(fld[i][2].buf);
    }

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (alarm_fired) {
            alarm_fired = 0;
            if (timer_running && timer_remaining > 0) {
                timer_remaining--;
                if (timer_remaining == 0) {
                    timer_done = 1;
                    timer_running = 0;
                    flash_t = 0;
                } else {
                    alarm(1);
                }
            }
        }
        if (timer_done) flash_t += dt;
        if (anim_running && !anim_done) {
            anim_time += dt * anim_speed;
            if (anim_time >= (float)sched_total_time) {
                anim_time = (float)sched_total_time;
                anim_done = 1;
                anim_running = 0;
            }
        }

        if (tab == TAB_SCHED) {
            float wheel = GetMouseWheelMove();
            scroll_y -= wheel * 40.0f;
            if (scroll_y < 0) scroll_y = 0;
        }

        BeginDrawing();
        ClearBackground(C_BG);
        for (int i = 0; i < SH; i++) {
            float r = (float)i / SH;
            Color lc = {(unsigned char)(8 + r * 10), (unsigned char)(8 + r * 4), (unsigned char)(24 + r * 20), 255};
            DrawLine(0, i, SW, i, lc);
        }

        DrawRectangle(0, 0, SW, TAB_H, (Color){8, 8, 24, 255});
        Rectangle t1 = {10, 10, 190, 36}, t2 = {208, 10, 200, 36};
        draw_btn(t1, "  Countdown Timer", tab == TAB_TIMER, hov(t1));
        draw_btn(t2, "  CPU Scheduler", tab == TAB_SCHED, hov(t2));
        if (clicked(t1)) {
            tab = TAB_TIMER;
            scroll_y = 0;
        }
        if (clicked(t2)) tab = TAB_SCHED;
        DrawLine(0, TAB_H, SW, TAB_H,(Color){40, 20, 70, 255});

        BeginScissorMode(0, TAB_H, SW, SH - TAB_H);

        if (tab == TAB_TIMER) {
            int ty0 = TAB_H + 12;
            draw_panel((Rectangle){30, ty0, 340, 160}, 0.08f);
            DrawText("COUNTDOWN TIMER", 48, ty0 + 14, 17, C_ACCENT);
            DrawText("Seconds:", 48, ty0 + 52, 14, C_GRAY);
            Rectangle tfr = {160, (float)(ty0 + 48), 140, 26};
            field_update(&timer_fld, tfr, 0);
            field_draw(&timer_fld, tfr, "e.g. 90");
            Rectangle bs = {48, (float)(ty0 + 84), 100, 32}, br = {162, (float)(ty0 + 84), 100, 32};
            draw_btn(bs, "START", timer_running, hov(bs) && !timer_running && !timer_done);
            draw_btn(br, "RESET", 0, hov(br) && timer_total > 0);
            if (clicked(bs) && !timer_running && !timer_done && timer_fld.len) {
                timer_total = fint(&timer_fld);
                if (timer_total > 0) {
                    timer_remaining = timer_total;
                    timer_running = 1;
                    timer_done = 0;
                    flash_t = 0;
                    alarm(1);
                }
            }
            if (clicked(br) && timer_total > 0) {
                alarm(0);
                timer_running = 0;
                timer_done = 0;
                timer_remaining = 0;
                timer_total = 0;
                flash_t = 0;
                memset(&timer_fld, 0, sizeof(timer_fld));
            }

            int dw = 68, dh = 118, g = 7, total_dw = dw * 4 + g * 2 * 4 + g * 3;
            int ox = (SW - total_dw) / 2, oy = ty0 + 158;
            draw_panel((Rectangle){ox - 28, oy - 18, total_dw + 56, dh + 36}, 0.1f);
            Color son = C_LBLUE, soff = C_DIMB, scc = C_VIOLET;
            if (timer_done) {
                int bk = (int)(flash_t * 3) % 2;
                son = bk ? C_RED : (Color){255, 255, 255, 255};
                soff = C_DIMV;
                scc = bk ? C_RED : C_VIOLET;
            } else if (timer_total > 0) {
                float p = (float)timer_remaining / timer_total;
                if (p < 0.25f) son = C_RED;
                else if (p < 0.5f) son = C_ORANGE;
            }
            draw_clock(timer_remaining, ox, oy, dw, dh, g, son, soff, scc);
            RRL((Rectangle){ox - 28, oy - 18, total_dw + 56, dh + 36}, 0.1f, 8, C_VIOLET);

            if (timer_total > 0) {
                float p = (float)timer_remaining / timer_total;
                int bx = 60, by = oy + dh + 36, bw = SW - 120, bh = 12;
                DrawRectangleRounded((Rectangle){bx, by, bw, bh}, 0.5f, 4, C_DIMV);
                Color pc = p > 0.5f ? C_LBLUE : p > 0.25f ? C_ORANGE : C_RED;
                DrawRectangleRounded((Rectangle){bx, by, (int)(bw * p), bh}, 0.5f, 4, pc);
                RRL((Rectangle){bx, by, bw, bh}, 0.5f, 4, C_VIOLET);
                char ps[32];
                sprintf(ps, "%d / %d s", timer_remaining, timer_total);
                int tw = MeasureText(ps, 13);
                DrawText(ps, (SW - tw) / 2, by + 16, 13, C_GRAY);
                int msg_y = by + 36;
                if (timer_done && flash_t < 8.0f) {
                    int bk = (int)(flash_t * 3) % 2;
                    const char* msg = "TIME'S UP!";
                    int fs = 52, tw2 = MeasureText(msg, fs);
                    DrawText(msg, (SW - tw2) / 2, msg_y, fs, bk ? C_RED : (Color){255, 255, 255, 255});
                }
                if (timer_running) {
                    char rs[32];
                    sprintf(rs, "Running... %ds left", timer_remaining);
                    int tw2 = MeasureText(rs, 13);
                    DrawText(rs, (SW - tw2) / 2, msg_y, 13, C_GREEN);
                }
            }
        }

        if (tab == TAB_SCHED) {
            int dry_extra = 32;
            if (algo == SJF || algo == PRIO) dry_extra += 28;
            if (algo == RR) dry_extra += 28;
            int dry_hy = dry_extra + 6;
            int dry_add_y = dry_hy + 20 + nproc * 34 + 8 + (nproc < MAX_PROC ? 32 : 0);
            int dry_btn_y = dry_add_y + 8;
            int dry_speed_y = dry_btn_y + 42;
            int right_base = 34;
            int right_panel_top = 6;
            int row_h = 34;
            int right_gy = right_base + 14 + (nproc + 1) * row_h;
            int right_gh = 28;
            int right_sy = right_gy + right_gh + 14;
            int right_bottom = right_sy + 20 + nproc * 22 + 30;
            int left_bottom = sched_ran ? (dry_speed_y + 30) : (dry_btn_y + 50);
            int total_virtual_h = left_bottom > right_bottom + right_panel_top ? left_bottom : right_bottom + right_panel_top;
            int visible_h = SH - TAB_H;
            float max_scroll = (float)(total_virtual_h - visible_h);
            if (max_scroll < 0) max_scroll = 0;
            if (scroll_y > max_scroll) scroll_y = max_scroll;

            draw_panel((Rectangle){8, (float)VY(6), 390, (float)(total_virtual_h - 6)}, 0.05f);

            const char* an[] = {"FCFS", "SJF", "RR", "Priority"};
            int algo_tab_y = 12;
            for (int i = 0; i < 4; i++) {
                Rectangle ab = {16 + (float)i * 92, (float)VY(algo_tab_y), 86, 26};
                draw_btn(ab, an[i], algo == i, hov(ab));
                if (clicked(ab)) {
                    algo = i;
                    sched_ran = 0;
                    anim_running = 0;
                    anim_done = 0;
                    anim_time = 0;
                }
            }

            int extra_y = algo_tab_y + 32;

            if (algo == SJF) {
                DrawText("Mode:", 16, VY(extra_y + 2), 12, C_GRAY);
                Rectangle rnp = {60, (float)VY(extra_y), 90, 22};
                Rectangle rp = {158, (float)VY(extra_y), 130, 22};
                draw_toggle(rnp, "Non-Preempt", !sjf_preemptive);
                draw_toggle(rp, "Preempt (SRTF)", sjf_preemptive);
                if (clicked(rnp) && sjf_preemptive) {
                    sjf_preemptive = 0;
                    sched_ran = 0;
                    anim_time = 0;
                    anim_running = 0;
                    anim_done = 0;
                }
                if (clicked(rp) && !sjf_preemptive) {
                    sjf_preemptive = 1;
                    sched_ran = 0;
                    anim_time = 0;
                    anim_running = 0;
                    anim_done = 0;
                }
                extra_y += 28;
            }
            if (algo == PRIO) {
                DrawText("Mode:", 16, VY(extra_y + 2), 12, C_GRAY);
                Rectangle rnp = {60, (float)VY(extra_y), 90, 22};
                Rectangle rp = {158, (float)VY(extra_y), 100, 22};
                draw_toggle(rnp, "Non-Preempt", !prio_preemptive);
                draw_toggle(rp, "Preemptive", prio_preemptive);
                if (clicked(rnp) && prio_preemptive) {
                    prio_preemptive = 0;
                    sched_ran = 0;
                    anim_time = 0;
                    anim_running = 0;
                    anim_done = 0;
                }
                if (clicked(rp) && !prio_preemptive) {
                    prio_preemptive = 1;
                    sched_ran = 0;
                    anim_time = 0;
                    anim_running = 0;
                    anim_done = 0;
                }
                extra_y += 28;
            }
            if (algo == RR) {
                DrawText("Quantum:", 16, VY(extra_y + 2), 12, C_GRAY);
                Rectangle qr = {80, (float)VY(extra_y), 50, 22};
                field_update(&quantum_fld, qr, 0);
                field_draw(&quantum_fld, qr, "2");
                rr_quantum = fint(&quantum_fld);
                if (rr_quantum < 1) rr_quantum = 1;
                extra_y += 28;
            }

            int hy = extra_y + 6;
            DrawText("Name", 22, VY(hy), 12, C_GRAY);
            DrawText("Arr", 78, VY(hy), 12, C_GRAY);
            DrawText("Burst", 130, VY(hy), 12, C_GRAY);
            DrawText("Prio", 190, VY(hy), 12, C_GRAY);
            DrawLine(14, VY(hy + 16), 396, VY(hy + 16), (Color){40, 20, 70, 255});

            for (int i = 0; i < nproc; i++) {
                int ry = hy + 20 + i * 34;
                DrawRectangle(14, VY(ry) - 2, 380, 30, i % 2 == 0 ? (Color){20, 12, 38, 255} : (Color){16, 10, 30, 255});
                DrawCircle(26, VY(ry) + 12, 5, procs[i].col);
                Rectangle fn = {36, (float)VY(ry + 2), 48, 22};
                field_update(&fld[i][3], fn, 1);
                field_draw(&fld[i][3], fn, "P?");
                Rectangle fa = {90, (float)VY(ry + 2), 46, 22};
                field_update(&fld[i][0], fa, 0);
                field_draw(&fld[i][0], fa, "0");
                Rectangle fb = {142, (float)VY(ry + 2), 46, 22};
                field_update(&fld[i][1], fb, 0);
                field_draw(&fld[i][1], fb, "1");
                Rectangle fp = {196, (float)VY(ry + 2), 46, 22};
                field_update(&fld[i][2], fp, 0);
                field_draw(&fld[i][2], fp, "1");
                Rectangle del = {252, (float)VY(ry + 3), 44, 20};
                draw_btn(del, "Del", 0, hov(del));
                if (clicked(del) && nproc > 1) {
                    for (int j = i; j < nproc - 1; j++) {
                        procs[j] = procs[j + 1];
                        memcpy(fld[j], fld[j + 1], sizeof(fld[0]));
                    }
                    nproc--;
                    sched_ran = 0;
                    anim_running = 0;
                    anim_done = 0;
                    anim_time = 0;
                }
            }

            for (int i = 0; i < nproc; i++) {
                strncpy(procs[i].name, fld[i][3].buf, 7);
                procs[i].arrival = fint(&fld[i][0]);
                procs[i].burst = fint(&fld[i][1]);
                if (procs[i].burst < 1) procs[i].burst = 1;
                procs[i].priority = fint(&fld[i][2]);
                if (procs[i].priority < 1) procs[i].priority = 1;
                procs[i].col = PCOLS[i % 12];
            }

            int add_y = hy + 20 + nproc * 34 + 8;
            if (nproc < MAX_PROC) {
                Rectangle ab2 = {16, (float)VY(add_y), 130, 26};
                draw_btn(ab2, "+ Add Process", 0, hov(ab2));
                if (clicked(ab2)) {
                    int i = nproc++;
                    snprintf(procs[i].name, 8, "P%d", i + 1);
                    procs[i].arrival = 0;
                    procs[i].burst = 3;
                    procs[i].priority = 1;
                    procs[i].remaining = 3;
                    procs[i].col = PCOLS[i % 12];
                    snprintf(fld[i][3].buf, 32, "%s", procs[i].name);
                    fld[i][3].len = strlen(fld[i][3].buf);
                    strcpy(fld[i][0].buf, "0");
                    fld[i][0].len = 1;
                    strcpy(fld[i][1].buf, "3");
                    fld[i][1].len = 1;
                    strcpy(fld[i][2].buf, "1");
                    fld[i][2].len = 1;
                    sched_ran = 0;
                    anim_running = 0;
                    anim_done = 0;
                    anim_time = 0;
                }
                add_y += 32;
            }

            int btn_y = add_y + 8;
            Rectangle run_r = {14, (float)VY(btn_y), 175, 34};
            const char* albl[] = {"Run FCFS", "Run SJF", "Run Round Robin", "Run Priority"};
            draw_btn(run_r, albl[algo], 0, hov(run_r));
            if (clicked(run_r)) {
                run_schedule(algo);
                sched_ran = 1;
                anim_running = 0;
                anim_done = 0;
                anim_time = 0;
            }

            if (sched_ran) {
                Rectangle play_r = {198, (float)VY(btn_y), 76, 34};
                draw_btn(play_r, anim_running ? "Pause" : "Play", anim_running, hov(play_r));
                if (clicked(play_r)) {
                    if (anim_done) {
                        anim_time = 0;
                        anim_done = 0;
                    }
                    anim_running = !anim_running;
                }
                Rectangle rst_r = {282, (float)VY(btn_y), 76, 34};
                draw_btn(rst_r, "Restart", 0, hov(rst_r));
                if (clicked(rst_r)) {
                    anim_time = 0;
                    anim_running = 0;
                    anim_done = 0;
                }

                int speed_y = btn_y + 42;
                DrawText("Speed:", 14, VY(speed_y), 12, C_GRAY);
                float speeds[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
                const char* slbl[] = {"0.5x", "1x", "2x", "4x", "8x"};
                for (int i = 0; i < 5; i++) {
                    Rectangle sr = {62 + (float)i * 60, (float)VY(speed_y + 2), 54, 22};
                    draw_btn(sr, slbl[i], anim_speed == speeds[i], hov(sr));
                    if (clicked(sr)) anim_speed = speeds[i];
                }
            }

            const char* desc[4] = {
                "Non-preemptive. Ordered by arrival.",
                sjf_preemptive ? "Preemptive (SRTF). Shorter remaining burst wins." : "Non-preemptive. Shortest burst selected first.",
                "Preemptive. Rotates with time quantum.",
                prio_preemptive ? "Preemptive Priority. Lower number = higher priority." : "Non-preemptive Priority. Lower number = higher priority."
            };
            DrawText(desc[algo], 14, VY(left_bottom - 18), 11, C_GRAY);

            int rx = 404, rw = SW - rx - 8;
            draw_panel((Rectangle){rx, (float)VY(right_panel_top), rw, (float)(total_virtual_h - right_panel_top)}, 0.05f);

            if (!sched_ran) {
                const char* h = "Set up processes and press Run.";
                int tw = MeasureText(h, 15);
                DrawText(h, rx + (rw - tw) / 2, VY(right_panel_top + 200), 15, C_GRAY);
            } else {
                char tstr[32];
                snprintf(tstr, 32, "t = %d / %d", (int)anim_time, sched_total_time);
                int tw2 = MeasureText(tstr, 20);
                DrawText(tstr, rx + rw - tw2 - 10, VY(right_panel_top + 8), 20, C_ACCENT);

                DrawRectangle(rx + 10, VY(right_panel_top+10), 14, 12, (Color){40, 40, 40, 200});
                DrawText("Idle", rx + 28, VY(right_panel_top + 10), 11, C_GRAY);
                DrawLine(rx + 70, VY(right_panel_top + 10), rx + 70, VY(right_panel_top + 22), C_RED);
                DrawText("Interrupt", rx + 74, VY(right_panel_top + 10), 11, C_RED);

                int bar_lbl = 38;
                int bax = rx + 10 + bar_lbl;
                int baw = rw - bar_lbl - 20;
                int bay_start = right_panel_top + right_base;
                int bar_h = 22;
                float ppt = (float)baw / (float)(sched_total_time > 0 ? sched_total_time : 1);

                int tick_step = 1;
                if (sched_total_time > 60) tick_step = 10;
                else if (sched_total_time > 30) tick_step = 5;
                else if (sched_total_time > 15) tick_step = 2;
                
                for (int t = 0; t <= sched_total_time; t += tick_step) {
                    int tx = bax + (int)(t * ppt);
                    DrawLine(tx, VY(bay_start + 2), tx, VY(bay_start + 6), (Color){80, 60, 120, 255});
                    char ts[8];
                    snprintf(ts, 8, "%d", t);
                    int tsw = MeasureText(ts, 9);
                    DrawText(ts, tx - tsw / 2, VY(bay_start - 8), 9, C_GRAY);
                }

                for (int i = 0; i < nproc; i++) {
                    int ry = bay_start + 10 + i * row_h;
                    DrawText(procs[i].name, rx + 12, VY(ry + 4), 13, C_WHITE);
                    DrawRectangle(bax, VY(ry), baw, bar_h, (Color){20, 12, 38, 255});

                    if (procs[i].arrival <= sched_total_time) {
                        int ax = bax + (int)(procs[i].arrival * ppt);
                        DrawLine(ax, VY(ry), ax, VY(ry + bar_h), (Color){255, 230, 50, 200});
                    }

                    int first_start = sched_total_time;
                    for (int s = 0; s < sched_n; s++) {
                        if (sched_full[s].pid == i) {
                            first_start = sched_full[s].start;
                            break;
                        }
                    }
                    if (first_start > procs[i].arrival) {
                        float wend = (float)first_start < anim_time ? (float)first_start : anim_time;
                        if (wend > (float)procs[i].arrival && (float)procs[i].arrival <= anim_time) {
                            int wx = bax + (int)(procs[i].arrival * ppt);
                            int ww = (int)((wend - procs[i].arrival) * ppt);
                            DrawRectangle(wx, VY(ry + bar_h / 2 - 2), ww, 4, (Color){180, 180, 0, 80});
                        }
                    }

                    for (int s = 0; s < sched_n; s++) {
                        if (sched_full[s].pid != i) continue;
                        float gs = (float)sched_full[s].start, ge = (float)sched_full[s].end;
                        float vis = ge < anim_time ? ge : anim_time;
                        if (vis <= gs) continue;
                        int sx = bax + (int)(gs * ppt);
                        int sw2 = (int)((vis - gs) * ppt);
                        if (sw2 < 1) sw2 = 1;
                        DrawRectangle(sx, VY(ry), sw2, bar_h, procs[i].col);
                        if (anim_time >= gs && anim_time < ge) {
                            int gx = bax + (int)(anim_time * ppt) - 2;
                            DrawRectangle(gx, VY(ry), 5, bar_h, (Color){255, 255, 255, 160});
                        }
                        char bl[8];
                        snprintf(bl, 8, "%s", procs[i].name);
                        int blw = MeasureText(bl, 11);
                        if (sw2 > blw + 4) DrawText(bl, sx + (sw2 - blw) / 2, VY(ry + (bar_h - 11) / 2), 11, (Color){10, 10, 30, 255});
                        if (sched_full[s].is_interrupt && vis >= ge) {
                            int ix = bax + (int)(ge * ppt);
                            DrawLine(ix, VY(ry), ix, VY(ry + bar_h), C_RED);
                            DrawTriangle((Vector2){ix - 4, (float)VY(ry)}, (Vector2){ix + 4, (float)VY(ry)}, (Vector2){ix, (float)VY(ry + 7)}, C_RED);
                        }
                    }
                    DrawRectangleLines(bax, VY(ry), baw, bar_h, (Color){40, 20, 70, 255});

                    if (procs[i].finish > 0 && (float)procs[i].finish <= anim_time) {
                        DrawText("v", rx + 12 + MeasureText(procs[i].name, 13) + 4, VY(ry + 4), 13, C_GREEN);
                    }
                }

                {
                    int ry_idle = bay_start + 10 + nproc * row_h;
                    DrawText("Idle", rx + 12, VY(ry_idle + 4), 11, C_GRAY);
                    DrawRectangle(bax, VY(ry_idle), baw, 14, (Color){20, 12, 38, 255});
                    for (int s = 0; s < sched_n; s++) {
                        if (sched_full[s].pid != -1) continue;
                        float gs = (float)sched_full[s].start, ge = (float)sched_full[s].end;
                        float vis = ge < anim_time ? ge : anim_time;
                        if (vis <= gs) continue;
                        int ix2 = bax + (int)(gs * ppt);
                        int iw = (int)((vis - gs) * ppt);
                        if (iw < 1) iw = 1;
                        DrawRectangle(ix2, VY(ry_idle), iw, 14, (Color){35, 35, 35, 220});
                        for (int xx = ix2; xx < ix2 + iw; xx += 6) {
                            DrawLine(xx, VY(ry_idle), xx + 4, VY(ry_idle + 14), (Color){80, 80, 80, 160});
                        }
                        DrawRectangleLines(ix2, VY(ry_idle), iw, 14, C_GRAY);
                    }
                    DrawRectangleLines(bax, VY(ry_idle), baw, 14, (Color){40, 20, 70, 255});
                }

                {
                    int cx = bax + (int)(anim_time * ppt);
                    int top2 = bay_start, bot2 = bay_start + 10 + (nproc + 1) * row_h;
                    DrawLine(cx, VY(top2), cx, VY(bot2), (Color){255, 50, 50, 200});
                    DrawTriangle((Vector2){cx - 5, (float)VY(top2)}, (Vector2){cx + 5, (float)VY(top2)}, (Vector2){cx, (float)VY(top2 + 8)}, C_RED);
                }

                int gy = bay_start + 14 + (nproc + 1) * row_h;
                DrawLine(rx, VY(gy), rx + rw, VY(gy), (Color){40, 20, 70, 255});
                DrawText("Gantt:", rx + 12, VY(gy + 6), 12, C_GRAY);
                int gh = 28, gya = gy + 22;
                for (int s = 0; s < sched_n; s++) {
                    float gs = (float)sched_full[s].start, ge = (float)sched_full[s].end;
                    float vis = ge < anim_time ? ge : anim_time;
                    if (vis <= gs) continue;
                    int gx = bax + (int)(gs * ppt);
                    int gw = (int)((vis - gs) * ppt);
                    if (gw < 1) gw = 1;
                    int pid2 = sched_full[s].pid;
                    if (pid2 == -1) {
                        DrawRectangle(gx, VY(gya), gw, gh, (Color){35, 35, 35, 220});
                        for (int xx = gx; xx < gx + gw; xx += 6) {
                            DrawLine(xx, VY(gya), xx + 4, VY(gya + gh), (Color){80, 80, 80, 160});
                        }
                        DrawRectangleLines(gx, VY(gya), gw, gh, C_GRAY);
                        char il[4] = "---";
                        int ilw = MeasureText(il, 9);
                        if (gw > ilw + 2) DrawText(il, gx + (gw - ilw) / 2, VY(gya + (gh - 9) / 2), 9, C_GRAY);
                    } else {
                        DrawRectangle(gx, VY(gya), gw, gh, procs[pid2].col);
                        char lbl[8];
                        snprintf(lbl, 8, "%s", procs[pid2].name);
                        int lw = MeasureText(lbl, 10);
                        if (gw > lw + 2) DrawText(lbl, gx + (gw - lw) / 2, VY(gya + (gh - 10) / 2), 10, (Color){10, 10, 30, 255});
                        if (sched_full[s].is_interrupt && vis >= ge) {
                            int ixx = gx + gw;
                            DrawLine(ixx, VY(gya), ixx, VY(gya + gh), C_RED);
                        }
                    }
                }
                DrawRectangleLines(bax, VY(gya), baw, gh, (Color){40, 20, 70, 255});

                int sy = gya + gh + 14;
                DrawLine(rx, VY(sy - 4), rx + rw, VY(sy - 4), (Color){40, 20, 70, 255});
                int c0 = rx + 12, c1 = c0 + 44, c2 = c1 + 52, c3 = c2 + 56, c4 = c3 + 64, c5 = c4 + 64;
                DrawText("Proc", c0, VY(sy), 11, C_GRAY);
                DrawText("Arr", c1, VY(sy), 11, C_GRAY);
                DrawText("Burst", c2, VY(sy), 11, C_GRAY);
                DrawText("Finish", c3, VY(sy), 11, C_GRAY);
                DrawText("Waiting", c4, VY(sy), 11, C_GRAY);
                DrawText("TAT", c5, VY(sy), 11, C_GRAY);
                DrawLine(rx + 8, VY(sy + 14), rx + rw - 8, VY(sy + 14), (Color){40, 20, 70, 255});

                for (int i = 0; i < nproc; i++) {
                    int ty = sy + 20 + i * 22;
                    DrawRectangle(rx + 8, VY(ty - 2), rw - 16, 18, i % 2 == 0 ? (Color){18, 10, 34, 200} : (Color){14, 8, 28, 200});
                    DrawCircle(c0 + 5, VY(ty + 7), 4, procs[i].col);
                    DrawText(procs[i].name, c0 + 13, VY(ty), 12, C_WHITE);
                    char tmp[16];
                    sprintf(tmp, "%d", procs[i].arrival);    DrawText(tmp, c1, VY(ty), 12, C_WHITE);
                    sprintf(tmp, "%d", procs[i].burst);      DrawText(tmp, c2, VY(ty), 12, C_WHITE);
                    int pdone = procs[i].finish > 0 && (float)procs[i].finish <= anim_time;
                    if (pdone) {
                        sprintf(tmp, "%d", procs[i].finish);      DrawText(tmp, c3, VY(ty), 12, C_GREEN);
                        sprintf(tmp, "%d", procs[i].waiting);     DrawText(tmp, c4, VY(ty), 12, C_ORANGE);
                        sprintf(tmp, "%d", procs[i].turnaround);  DrawText(tmp, c5, VY(ty), 12, C_LBLUE);
                    } else {
                        DrawText("--", c3, VY(ty), 12, C_GRAY);
                        DrawText("--", c4, VY(ty), 12, C_GRAY);
                        DrawText("--", c5, VY(ty), 12, C_GRAY);
                    }
                }

                if (anim_done) {
                    int ay = sy + 20 + nproc * 22 + 6;
                    DrawLine(rx + 8, VY(ay), rx + rw - 8, VY(ay), (Color){40, 20, 70, 255});
                    char av[100];
                    snprintf(av, 100, "Avg Waiting: %.2f    Avg Turnaround: %.2f", avg_wait, avg_tat);
                    DrawText(av, c0, VY(ay + 6), 12, C_ACCENT);
                }
            }

            if (max_scroll > 0) {
                int sb_x = SW - 8, sb_h = SH - TAB_H - 4, sb_y = TAB_H + 2;
                DrawRectangle(sb_x, sb_y, 6, sb_h, (Color){20, 15, 40, 200});
                float ratio = (float)visible_h / (float)(total_virtual_h);
                int th = (int)(sb_h * ratio);
                if (th < 24) th = 24;
                int ty2 = sb_y + (int)((scroll_y / max_scroll) * (sb_h - th));
                DrawRectangleRounded((Rectangle){(float)sb_x, (float)ty2, 6, (float)th}, 0.5f, 4, C_VIOLET);
            }
        } 

        EndScissorMode();

        DrawRectangle(0, 0, SW, TAB_H, (Color){8, 8, 24, 240});
        DrawLine(0, TAB_H, SW, TAB_H, (Color){40, 20, 70, 255});
        draw_btn((Rectangle){10, 10, 190, 36}, "  Countdown Timer", tab == TAB_TIMER, hov((Rectangle){10, 10, 190, 36}));
        draw_btn((Rectangle){208, 10, 200, 36}, "  CPU Scheduler", tab == TAB_SCHED, hov((Rectangle){208, 10, 200, 36}));

        EndDrawing();
    }
    CloseWindow();
    return 0;
}
