/* Minimal host stubs so the real GPStime.c compiles and runs on Linux. */
#ifndef STUB_PICO_STDLIB_H
#define STUB_PICO_STDLIB_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned int uint;
typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t, void *);
typedef void (*gpio_irq_callback_t)(uint, uint32_t);
#define __not_in_flash_func(f) f
#define __not_in_flash(s)
#define GPIO_IN 0
#define GPIO_FUNC_UART 2
#define GPIO_IRQ_EDGE_RISE 8u
#define UART0_IRQ 33
#define UART1_IRQ 34
#define UART_PARITY_NONE 0
extern uint64_t stub_now_us;
extern int stub_irq_enabled, stub_alarm_pending; extern alarm_callback_t stub_alarm_cb;
struct stub_timer { uint32_t timelr, timehr; };
extern struct stub_timer stub_timer_hw;
#define timer_hw (&stub_timer_hw)
static inline uint64_t time_us_64(void) { return stub_now_us; }
static inline void gpio_init(uint g) { (void)g; }
static inline void gpio_set_dir(uint g, bool o) { (void)g; (void)o; }
static inline void gpio_set_function(uint g, int f) { (void)g; (void)f; }
static inline void gpio_set_irq_enabled_with_callback(uint g, uint32_t e, bool en, gpio_irq_callback_t cb) { (void)g; (void)e; (void)cb; stub_irq_enabled = en; }
static inline void gpio_set_irq_enabled(uint g, uint32_t e, bool en) { (void)g; (void)e; stub_irq_enabled = en; }
static inline void gpio_acknowledge_irq(uint g, uint32_t e) { (void)g; (void)e; }
static inline alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t cb, void *u, bool f) { (void)us; (void)u; (void)f; stub_alarm_cb = cb; stub_alarm_pending = 1; return 1; }
static inline void irq_set_exclusive_handler(int n, void (*h)(void)) { (void)n; (void)h; }
static inline void irq_set_enabled(int n, bool e) { (void)n; (void)e; }
#define IO_IRQ_BANK0 13
static inline void irq_set_priority(int n, uint8_t p) { (void)n; (void)p; }

#include <assert.h>
#include <assert.h>
#endif
