#ifndef STUB_UART_H
#define STUB_UART_H
#include "pico/stdlib.h"
typedef struct { int id; } uart_inst_t;
extern uart_inst_t stub_uart0, stub_uart1;
#define uart0 (&stub_uart0)
#define uart1 (&stub_uart1)
extern const char *stub_rx; 
static inline void uart_init(uart_inst_t *u, uint b) { (void)u; (void)b; }
static inline void uart_set_hw_flow(uart_inst_t *u, bool a, bool b) { (void)u; (void)a; (void)b; }
static inline void uart_set_format(uart_inst_t *u, int a, int b, int c) { (void)u; (void)a; (void)b; (void)c; }
static inline void uart_set_fifo_enabled(uart_inst_t *u, bool e) { (void)u; (void)e; }
static inline void uart_set_irq_enables(uart_inst_t *u, bool a, bool b) { (void)u; (void)a; (void)b; }
static inline bool uart_is_readable(uart_inst_t *u) { (void)u; return *stub_rx; }
static inline char uart_getc(uart_inst_t *u) { (void)u; return *stub_rx++; }
#endif
