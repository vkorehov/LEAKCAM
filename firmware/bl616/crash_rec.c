#include "crash_rec.h"

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "wdg_guard.h"

#define CRASH_MAGIC 0x43524153u    /* "CRAS" */
#define BT_N        24             /* return-address candidates from the stack */
#define SCAN_WORDS  256            /* stack words scanned for them (1 KB) */
/* the hang detector fires this long after the last feed, before the hardware watchdog resets at WDG_GUARD_MS */
#define HANG_MS     (WDG_GUARD_MS - 2000u)

enum { K_EXCEPTION = 1, K_HANG, K_ASSERT, K_STACK, K_MALLOC };
static const char *const kind_name[] = { "?", "exception", "watchdog hang", "assert", "stack overflow",
                                         "malloc failed" };

static struct {
    uint32_t magic, stage, kind, count, uptime_ms, scanned;
    uint32_t mcause, mepc, mtval, ra, sp;
    char task[16];
    uint32_t n_bt, bt[BT_N];
} rec __attribute__((section(".hbn_noinit")));

static volatile uint32_t last_feed_ms;
/* every store reaches RAM before the next step: a recorder that hangs part-way leaves its real stage */
#define STAGE(n) do { __asm__ volatile("" ::: "memory"); rec.stage = (n); __asm__ volatile("" ::: "memory"); } while (0)
static volatile int recording;

/* the code: in flash (XIP) and the part copied to RAM (FreeRTOS, parts of lwIP), bl616_leakcam.ld.in */
extern uint8_t __rom_text_start__[], __rom_text_end__[], __tcm_text_start__[], __tcm_text_end__[];

extern uint8_t __ram_start__[], __HeapLimit[];

static int is_code(uint32_t a)
{
    return !(a & 1) && ((a >= (uint32_t)__rom_text_start__ && a < (uint32_t)__rom_text_end__) ||
                        (a >= (uint32_t)__tcm_text_start__ && a < (uint32_t)__tcm_text_end__));
}

/* no frame pointers in this build: every word on the stack that points into the code is a return-address candidate;
 * the host decodes them (most are the real call chain, a few are stale). Only inside the RAM: a read past its end
 * stalls the bus like a store to 0 and the record was never finished (2026-10-31). */
static void scan_stack(uint32_t sp)
{
    rec.n_bt = 0;
    STAGE(3);
    if (sp & 3 || sp < (uint32_t)__ram_start__ || sp >= (uint32_t)__HeapLimit)
        return;
    uint32_t words = ((uint32_t)__HeapLimit - sp) / 4;
    for (uint32_t i = 0; i < SCAN_WORDS && i < words && rec.n_bt < BT_N; i++) {
        ((volatile uint32_t *)&rec.scanned)[0] = i;
        uint32_t w = ((volatile uint32_t *)sp)[i];
        if (is_code(w))
            rec.bt[rec.n_bt++] = w;
    }
}

static void begin(uint32_t kind)
{
    uint32_t n = rec.magic == CRASH_MAGIC ? rec.count : 0;
    memset(&rec, 0, sizeof(rec));
    rec.count = n + 1;
    rec.kind = kind;
    rec.magic = CRASH_MAGIC;                /* first: a record cut short still shows how far it got (stage) */
    STAGE(1);
    rec.uptime_ms = (uint32_t)bflb_mtimer_get_time_ms();
    TaskHandle_t t = xTaskGetCurrentTaskHandle();
    strncpy(rec.task, t ? pcTaskGetName(t) : "(none)", sizeof(rec.task) - 1);
    STAGE(2);
}

static void end(void)
{
    STAGE(9);                               /* complete */
}

/* the trap entry through -Wl,--wrap=exception_entry, from two callers (2026-10-31: reading a0 as a pointer under
 * FreeRTOS stalled the bus and the record stopped half-way):
 * - the SDK's vector.S (before the scheduler): a0 = the saved registers, regs[0] mepc, [1] ra, [2] sp;
 * - FreeRTOS portASM.S (once it runs): a0 = mcause; the task's context is on its stack (pxTopOfStack: [0] mepc + 4,
 *   [2] ra, portContext.h). mepc is read from the CSR in both cases: still the faulting instruction. */
void __real_exception_entry(uintptr_t *regs);
void __wrap_exception_entry(uintptr_t *regs)
{
    uint32_t cause, epc, tval;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    if ((cause & 0x3ff) != 8 && (cause & 0x3ff) != 11 && !recording) {   /* ecalls are not faults */
        recording = 1;
        __asm__ volatile("csrr %0, mepc" : "=r"(epc));
        __asm__ volatile("csrr %0, mtval" : "=r"(tval));
        begin(K_EXCEPTION);
        rec.mcause = cause;
        rec.mepc = epc;
        rec.mtval = tval;
        STAGE(5);
        uint32_t a0 = (uint32_t)regs;
        if (!(a0 & 3) && a0 >= (uint32_t)__ram_start__ && a0 < (uint32_t)__HeapLimit) {
            rec.ra = regs[1];
            rec.sp = regs[2];
        } else {
            TaskHandle_t t = xTaskGetCurrentTaskHandle();
            uint32_t *top = t ? *(uint32_t **)t : NULL;
            if (top && (uint32_t)top >= (uint32_t)__ram_start__ && (uint32_t)top < (uint32_t)__HeapLimit) {
                rec.ra = top[2];
                rec.sp = (uint32_t)top;     /* the saved context, then the task's frames above it */
            }
        }
        STAGE(4);
        scan_stack(rec.sp);
        end();
    }
    __real_exception_entry(regs);
}

void crash_rec_fed(void)
{
    last_feed_ms = (uint32_t)bflb_mtimer_get_time_ms();
}

/* runs in the tick interrupt: the feeder (priority max - 1) has not run for HANG_MS, so something above it spins;
 * the interrupted task's saved context sits at the top of its stack (FreeRTOS RISC-V port: [0] mepc, [2] ra) */
void vApplicationTickHook(void)
{
    uint32_t now = (uint32_t)bflb_mtimer_get_time_ms();
    if (recording || !last_feed_ms || now - last_feed_ms < HANG_MS)
        return;
    recording = 1;
    begin(K_HANG);
    TaskHandle_t t = xTaskGetCurrentTaskHandle();
    if (t) {
        uint32_t *top = *(uint32_t **)t;    /* pxTopOfStack, the TCB's first member */
        rec.mepc = top[0];
        rec.ra = top[2];
        rec.sp = (uint32_t)top;
        scan_stack(rec.sp);
    }
    end();                                  /* the hardware watchdog resets ~2 s later */
}

static void record_here(uint32_t kind)
{
    if (recording)
        return;
    recording = 1;
    begin(kind);
    uint32_t sp, ra;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    __asm__ volatile("mv %0, ra" : "=r"(ra));
    rec.ra = ra;
    rec.sp = sp;
    scan_stack(sp);
    end();
}

extern void abort(void);

void vAssertCalled(void)
{
    record_here(K_ASSERT);
    printf("vAssertCalled\r\n");
    abort();
}

/* -Wl,--wrap=vApplicationStackOverflowHook: the Wi-Fi library (rtos_al.c) defines its own, which spins forever */
void __wrap_vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    record_here(K_STACK);
    strncpy(rec.task, pcTaskName, sizeof(rec.task) - 1);   /* the task that overflowed, not the one checking */
    printf("vApplicationStackOverflowHook %s\r\n", pcTaskName);
    abort();
}

void vApplicationMallocFailedHook(void)
{
    record_here(K_MALLOC);
    printf("vApplicationMallocFailedHook\r\n");
    abort();
}

/* fail fast on every allocation failure (user, 2026-11-01): FreeRTOS's pvPortMalloc calls the hook above, plain
 * malloc() (picolibc) returned NULL and ran on; linked with --wrap=malloc/calloc/realloc */
void *__real_malloc(size_t n);
void *__real_calloc(size_t k, size_t n);
void *__real_realloc(void *p, size_t n);

void *__wrap_malloc(size_t n)
{
    void *p = __real_malloc(n);
    if (p == NULL && n != 0)
        vApplicationMallocFailedHook();
    return p;
}

void *__wrap_calloc(size_t k, size_t n)
{
    void *p = __real_calloc(k, n);
    if (p == NULL && k != 0 && n != 0)
        vApplicationMallocFailedHook();
    return p;
}

void *__wrap_realloc(void *old, size_t n)
{
    void *p = __real_realloc(old, n);
    if (p == NULL && n != 0)
        vApplicationMallocFailedHook();
    return p;
}

static void print_rec(void)
{
    printf("crash #%lu: %s in task '%s' at %lu ms; mcause 0x%08lx mepc 0x%08lx mtval 0x%08lx ra 0x%08lx sp 0x%08lx\r\n",
           (unsigned long)rec.count, kind_name[rec.kind < 6 ? rec.kind : 0], rec.task, (unsigned long)rec.uptime_ms,
           (unsigned long)rec.mcause, (unsigned long)rec.mepc, (unsigned long)rec.mtval, (unsigned long)rec.ra,
           (unsigned long)rec.sp);
    if (rec.stage != 9)
        printf("crash record incomplete: the recorder stopped at stage %lu (1 begin, 2 task, 5 mcause, 4 registers, 3 stack scan)\r\n",
               (unsigned long)rec.stage);
    if (rec.stage != 9)
        printf("crash record: stage 4 = registers read; stack words scanned %lu\r\n", (unsigned long)rec.scanned);
    printf("crash backtrace:");
    for (uint32_t i = 0; i < rec.n_bt && i < BT_N; i++)
        printf(" 0x%08lx", (unsigned long)rec.bt[i]);
    printf("\r\n");
}

/* every boot while a record is kept: a crash never goes unseen; `crash clear` drops it */
void crash_rec_report(void)
{
    if (rec.magic == CRASH_MAGIC)
        print_rec();
}

#ifdef CONFIG_SHELL
#include "shell.h"

static void cmd_crash(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "clear")) {
        memset(&rec, 0, sizeof(rec));
        printf("crash: cleared\r\n");
    } else if (argc > 1 && !strcmp(argv[1], "test")) {   /* proves the exception path: an illegal instruction
                                                           * (a store to 0 stalls the bus instead: no trap) */
        printf("crash: illegal instruction now\r\n");
        bflb_mtimer_delay_ms(20);
        __asm__ volatile(".word 0");
    } else if (argc > 1 && !strcmp(argv[1], "hang")) {   /* proves the hang path: the feeder never runs again */
        printf("crash: scheduler suspended, spinning\r\n");
        bflb_mtimer_delay_ms(20);
        vTaskSuspendAll();
        for (;;) {
        }
    } else if (rec.magic == CRASH_MAGIC) {
        print_rec();
    } else {
        printf("crash: no record\r\n");
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_crash, crash, last crash record kept across resets [clear | test | hang]);
#endif
