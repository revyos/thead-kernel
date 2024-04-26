#undef TRACE_SYSTEM
#define TRACE_SYSTEM g2d

#if !defined(_TRACE_G2D_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_G2D_H

#include <linux/sched/numa_balancing.h>
#include <linux/tracepoint.h>
#include <linux/binfmts.h>

TRACE_EVENT(g2d_irq_reg,

       TP_PROTO(unsigned int irq_reg_addr, unsigned int reg_value),

       TP_ARGS(irq_reg_addr, reg_value),

       TP_STRUCT__entry(
               __field(        unsigned int,   irq_reg_addr )
               __field(        unsigned int,   reg_value)
       ),

       TP_fast_assign(
               __entry->irq_reg_addr = irq_reg_addr;
               __entry->reg_value = reg_value;
       ),

       TP_printk("irq_reg_addr%d reg_value=%d", __entry->irq_reg_addr, __entry->reg_value)
);

TRACE_EVENT(g2d_cur_freq,

       TP_PROTO(unsigned int cur_freq),

       TP_ARGS(cur_freq),

       TP_STRUCT__entry(
               __field(unsigned int,   cur_freq )
       ),

       TP_fast_assign(
               __entry->cur_freq = cur_freq;
       ),

       TP_printk("cur_freq%d", __entry->cur_freq)
);

#endif /* _TRACE_G2D_H */

/* This part must be outside protection */
#include <trace/define_trace.h>