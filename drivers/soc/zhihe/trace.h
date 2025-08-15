#if !defined(_TRACE_BMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BMU_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bmu

#include <linux/tracepoint.h>
#include "p100_bmu_type.h"

TRACE_EVENT (bmu_cnt_bytes_cycle_trans,
	     TP_PROTO (char *name, u8 ch, pft_event_array_t * pft_event),
	     TP_ARGS (name, ch, pft_event),
	     TP_STRUCT__entry (__field (u32, trac_num)
			       __field (u32, cont_num)
			       __field (u8, ch)
			       __array (char, name, 20)
			       __dynamic_array (u8, count,
						sizeof (bmu_count_data_t) *
						3)),
	     TP_fast_assign (memcpy (__entry->name, name, 4);
			     __entry->ch = ch;
			     __entry->trac_num = pft_event->trac_num;
			     __entry->cont_num = pft_event->cont_num;
			     memcpy (__get_dynamic_array (count),
				     pft_event->count_event,
				     sizeof (bmu_count_data_t) * 3);),
	     TP_printk ("bmu=%s chn=%u " "mod=%u id=%u "
			"rd_bytes=%u rd_cycle=%u rd_trans=%u "
			"wr_bytes=%u wr_cycle=%u wr_trans=%u ", __entry->name,
			__entry->ch,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			mod_info,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			usr_id,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			rd_bytes,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			rd_cycle,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			rd_trans,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			wr_bytes,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			wr_cycle,
			((bmu_count_data_t *) __get_dynamic_array (count))[0].
			wr_trans));


TRACE_EVENT (bmu_awb_arr_tim_delta,
	     TP_PROTO (char *name, u8 ch, pft_event_array_t * pft_event),
	     TP_ARGS (name, ch, pft_event),
	     TP_STRUCT__entry (__field (uint32_t, trac_num)
			       __field (uint32_t, cont_num)
			       __field (u8, ch)
			       __array (char, name, 20)
			       __dynamic_array (u8, trace,
						sizeof (bmu_trace_data_t) *
						3) __dynamic_array (u8, count,
								    sizeof
								    (bmu_count_data_t)
								    * 3)),
	     TP_fast_assign (memcpy (__entry->name, name, 4);
			     __entry->ch = ch;
			     __entry->trac_num = pft_event->trac_num;
			     __entry->cont_num = pft_event->cont_num;
			     memcpy (__get_dynamic_array (trace),
				     pft_event->trace_event,
				     sizeof (bmu_trace_data_t) * 3);
			     memcpy (__get_dynamic_array (count),
				     pft_event->count_event,
				     sizeof (bmu_count_data_t) * 3);),
	     TP_printk ("bmu=%s chn=%u " "mod=%u id=%u "
			"rd_rate=%u wr_rate=%u ", __entry->name, __entry->ch,
			((bmu_trace_data_t *) __get_dynamic_array (trace))[0].
			mod_info,
			((bmu_trace_data_t *) __get_dynamic_array (trace))[0].
			usr_id,
			((bmu_trace_data_t *) __get_dynamic_array (trace))[0].
			rd_rate,
			((bmu_trace_data_t *) __get_dynamic_array (trace))[0].
			wr_rate));
#endif /* _TRACE_BMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/soc/zhihe
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
