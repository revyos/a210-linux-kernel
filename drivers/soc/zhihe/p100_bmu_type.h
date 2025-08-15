#ifndef _P100_BMU_TYPE_H
#define _P100_BMU_TYPE_H
#include <linux/kernel.h>
//#include <linux/spinlock.h>

enum bmu3_category {
	BMU_DDR,
	BMU_GPU,
	BMU_NPU,
	BMU_PCIE,
	BMU_USB,
	BMU_VO,
	BMU_VI,
	BMU_VP,
	BMU_PERI,
	BMU_D2D,
	BMU_CATE_MAX
};

typedef enum {
	BMU_IDLE = 0,
	BMU_START,
	BMU_CNT_START,
	BMU_INT_TRIG,
	BMU_DAT_DEMUX,
	BMU_DELTA_CALCULATE,
	BMU_TRACE_EVENT,
	BMU_CNT_GET,
	BMU_CNT_CALC,
	BMU_CNT_UPDATE,
	BMU_CNT_EVENT,
	BMU_STOP,
	BMU_CNT_STOP,
	BMU_ABNORMAL
} bmu_state;

typedef enum {
	BMU_IO_IDLE = 0,
	BMU_IO_READY,
	BMU_IO_WRITE,
	BMU_IO_WRITING,
	BMU_IO_WEND,
} bmu_io_state;

typedef enum {
	EVENT_NONE = 0,
	CNT_TIME_EXPIRED,
	CNT_FILE_WRITE,
	CNT_TGT_DAT_OCCUR,
	CNT_ADDR_RD_OCCUR,
	CNT_RD_ERROR_RESP,
	CNT_WR_ERROR_RESP,
	TRACE_HALF_OCCUR,
	TRACE_FULL_OCCUR,
	TRACE_TIMER_OVEROUT,
	TRACE_AXI_RESP_ERR
} bmu_irq_flag;

typedef enum {
	DEVICE_ID_CFG = 1,
	MASTER_ID_CFG,
	PERIOD_CNT_CFG,
	DURA_THRESH_CFG,
	VR_FLT_CFG,
	VR_LEN_FLT_CFG,
	VR_ADDR_FLT_CFG,
	DURA_CMD_THRESH_CFG,
	TARGET_DATA_CFG,
	OSTD_CNT_MOD_CFG,
	SIDEBAND_LOOP_CFG,
	EXT_INT_CFG,
	TRIG_CFG,
	CAP_TRIG, //14
	CFG_MAX
} bmu_config;

#define FRAMEALL_LENGTH 64
struct bmu_trace_frame {
	u8 perf_data[FRAMEALL_LENGTH];
};

typedef enum {
	REC_TIM_START = 1,
	REC_TIM_STOP,
	REC_DATA,
	REC_ACT_MAX
} bmu_rec_action;

/* ===========per data ================
  data[0] --- rtr
  data[1] --- rdbyte
  data[2] --- rdu
  data[3] --- rthd
  data[4] --- rdly
  data[5] --- r_max_ot
  data[6] --- vrd
  data[7] --- wtr
  data[8] --- wdbyte
  data[9] --- wdu
  data[10] --- wthd
  data[11] --- wdly
  data[12] --- w_max_ot
  data[13] --- vwr
====================================*/
struct bm_data_info {
	u64 t_start;
	u64 t_stop;
	u64 perf_data[14];
};

struct bm_time_info {
	s64 ns;
	s64 us;
	s64 s;
};

struct cal_result {
	char *name;
	u32 cnt;
	u32 vr_cnt;
	u32 vw_cnt;
	u32 rsv;
};

struct smem_map_list {
	struct list_head map_head;
	spinlock_t lock;
	u32 inited;
};

struct smem_map {
	struct list_head map_list;
	struct task_struct *task;
	const void *mem;
	unsigned int count;
};

struct zh_memtester_device {
	struct device *dev;
	void __iomem *reg;
	bool lite;
	struct page **pages;
	struct sg_table *sgt;
	dma_addr_t wr_iova;
	phys_addr_t wr_pa;
};

//event out to user packet
struct event_usr_packet {
	u8 ext_type;
	u16 usr_id;
	u8 tim_delta_type;
	u16 tim_delta;
	u16 resv;
};

typedef struct {
	//size 16B
	u16 usrid;
	u16 size_para; //siz（high 8bit)|len(low 8bit)
	u32 load;
	u64 full_tim; //low 12bit-local tim, 12-53 sync
} trace_proc1_data_t;

typedef struct {
	//size 32B
	u16 usrid;
	u16 rate;
	u16 last_r_trans;
	u16 next_aw_trans;
	u64 start_tim;
	u64 end_tim;
	u64 resv;
} trace_proc2_data_t;

typedef struct {
	//size 40B
	u32 rd_bytes;
	u32 rd_cycle;
	u32 rd_trans;
	u32 wr_bytes;
	u32 wr_cycle;
	u32 wr_trans;
	u32 vrd_addr_cnt;
	u32 vwr_addr_cnt;
	u32 vrd_tgtdat_cnt;
	u32 vwr_tgtdat_cnt;
} cnt_proc1_data_t;

typedef struct {
	//size 40B
	u32 rd_bytes;
	u32 rd_cycle;
	u32 rd_trans;
	u32 wr_bytes;
	u32 wr_cycle;
	u32 wr_trans;
	u32 vrd_addr_cnt;
	u32 vwr_addr_cnt;
	u32 vrd_tgtdat_cnt;
	u32 vwr_tgtdat_cnt;
	//u32 rd_rate;
	//u32 rd_cycle_trans;
	//u32 wr_rate;
	//u32 wr_cycle_trans;
} cnt_proc2_data_t;

//for trace to perfetto
typedef struct {
	//size 32B
	u32 mod_info; //high 8bit resv
	u32 usr_id; //high 4bit resv
	u32 rd_bytes;
	u32 rd_cycle;
	u32 rd_trans;
	u32 wr_bytes;
	u32 wr_cycle;
	u32 wr_trans;
	u32 vrd_addr_cnt;
	u32 vwr_addr_cnt;
	u32 vrd_tgtdat_cnt;
	u32 vwr_tgtdat_cnt;
	u32 tal_vrd_addr_cnt;
	u32 tal_vwr_addr_cnt;
	u32 tal_vrd_tgtdat_cnt;
	u32 tal_vwr_tgtdat_cnt;
	//u32 tal_rd_bytes;
	//u32 tal_wr_bytes;
	//u32 tal_rd_trans;
	//u32 tal_wr_trans;
	//u32 tal_rd_cycle;
	//u32 tal_wr_cycle;
} bmu_count_data_t;

typedef struct {
	//size 64B
	u32 mod_info; //high 8bit resv
	u32 usr_id; //high 4bit resv
	u32 rd_rate; //GB/s
	u32 rd_last_r_trans;
	u32 rd_next_aw_trans;
	u32 wr_rate; //GB/s
	u32 wr_last_r_trans;
	u32 wr_next_aw_trans;
	u64 rd_start_tim;
	u64 rd_end_tim;
	u64 wr_start_tim;
	u64 wr_end_tim;
} bmu_trace_data_t;

typedef struct {
	//size 72B
	u32 mod_info; //high 8bit resv
	u32 usr_id; //high 4bit resv
	u8 data[128];
} bmu_capture_data_t;

typedef struct {
	u32 trac_num;
	u32 cont_num;
	atomic_t tal_addr_vrd;
	atomic_t tal_addr_vwr;
	atomic_t tal_tgtdat_vrd;
	atomic_t tal_tgtdat_vwr;
	bmu_trace_data_t trace_event[3];
	bmu_count_data_t count_event[3];
	bmu_capture_data_t cap_data;
} pft_event_array_t;

struct event_storage {
	size_t cnt;
	size_t tim_cnt;
	u64 last_tim;
	u8 *buf8;
	u32 *buf32;
	void *bufvi;
	phys_addr_t pa;
	trace_proc1_data_t *proc1_buf;
	trace_proc2_data_t *proc2_buf;
};

typedef struct {
	u32 align_reg0;
	u32 dura_thresh_reg04;
	u32 master_id_reg08;
	u32 period_cnt_reg0c;
	u32 vr_cnt_flt_reg10;
	u32 vr_cnt_len_reg14;
	u64 vr_up_addr_reg18;
	u64 vr_low_addr_reg2c;
	u32 cmd_dura_thresh_reg28;
	u32 target_data_reg3c;
	u32 target_comp_reg80;
	u32 ostd_cnt_reg94;
	u32 comp_mod_reg98;
	u32 loop_sband_regac;
	u32 devid_flt_regf0;
	u32 ext_int_regf8;
	u32 trg_sel_regfc;
	u32 trg_cond_reg100;
	u32 trg_cond_mask_reg104;
	uint32_t cap_trg;
} bmu_reg_data_t;

typedef struct {
	u32 type : 3;
	u32 awid : 11;
	u32 awaddr1 : 18; //32
	u32 awaddr2 : 22;
	u32 awlen : 8;
	u32 awsize1 : 2; //32
	u32 awsize2 : 1;
	u32 awburst : 2;
	u32 awlock : 1;
	u32 awcache : 4;
	u32 awprot : 3;
	u32 awqos : 4;
	u32 awregion : 4;
	u32 awvalid : 1;
	u32 awready : 1;
	u32 wdata1 : 11; //32
	u32 wdata2 : 32;
	u32 wdata3 : 32; //32
	u32 wdata4 : 32; //32
	u32 wdata5 : 21;
	u32 wstrab1 : 11; //32
	u32 wstrab2 : 5;
	u32 wlast : 1;
	u32 wvalid : 1;
	u32 wready : 1;
	u32 bid : 11;
	u32 bresp : 2;
	u32 bvalid : 1;
	u32 bready : 1;
	u32 arid1 : 9; //32
	u32 arid2 : 2;
	u32 araddr1 : 30; //32
	u32 araddr2 : 10;
	u32 arlen : 8;
	u32 arsize : 3;
	u32 arburst : 2;
	u32 arlock : 1;
	u32 arcache : 4;
	u32 arprot : 3;
	u32 arqos1 : 1; //32
	u32 arqos2 : 3;
	u32 arregion : 4;
	u32 arvalid : 1;
	u32 arready : 1;
	u32 rid : 11;
	u32 rdata1 : 12; //32
	u32 rdata2 : 32; //32
	u32 rdata3 : 32;
	u32 rdata4 : 32; //32
	u32 rdata5 : 20;
	u32 rresp : 2;
	u32 rlast : 1;
	u32 rvalid : 1;
	u32 rready : 1;
} Packet_Header;

#pragma pack(1)
/* trace packet type*/
/*88 bit*/
typedef struct {
	u8 type : 3;
	u8 ver : 3;
	u8 rls1 : 2; //8
	u8 rls2 : 1;
	u8 id : 4;
	u8 reuse_en : 1;
	u8 addr_reuse : 2; //8
	u8 tim_w : 5;
	u8 lat_w1 : 3; //8
	u8 lat_w2 : 2;
	u8 sync_id_w1 : 6; //8
	u8 sync_id_w2 : 1;
	u8 tig_mod : 1;
	u8 axi_id_w1 : 6; //8
	u8 axi_id_w2 : 2;
	u8 axi_addr_w1 : 6; //8
	u8 axi_addr_w2 : 2;
	u8 axi_data_w1 : 6; //8
	u8 axi_data_w2 : 2;
	u8 axi_len_w1 : 6; //8
	u8 axi_len_w2 : 2;
	u8 resv1 : 6; //8
	u8 resv2 : 8; //8
	u8 resv3 : 8; //8
} CONFIG_Frame_t;

/*44 bit little filed bit0*/
typedef struct {
	u8 type : 3;
	u8 sync1 : 5; //8
	u8 sync2 : 8;
	u8 sync3 : 8;
	u8 sync4 : 8;
	u8 sync5 : 8;
	u8 sync6 : 4;
	u8 resv : 4; //8
} SYNC_T_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 sync1 : 1; //8
	u8 sync2 : 8;
	u8 sync3 : 8;
	u8 sync4 : 8;
	u8 sync5 : 8;
	u8 sync6 : 8;
} SYNC_T_H_Frame_t;

/*88bit bit little filed bit0*/
typedef struct {
	u8 type : 3;
	u8 sync1 : 5; //8
	u8 sync2 : 8;
	u8 sync3 : 8;
	u8 sync4 : 8;
	u8 sync5 : 8;
	u8 sync6 : 4;
	u8 gltim1 : 4; //8
	u8 gltim2 : 8;
	u8 gltim3 : 8;
	u8 gltim4 : 8;
	u8 gltim5 : 8;
	u8 gltim6 : 8;
} EXT_SYNC_T_L_Frame_t;

/*88bit bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 sync1 : 1; //8
	u8 sync2 : 8;
	u8 sync3 : 8;
	u8 sync4 : 8;
	u8 sync5 : 8;
	u8 sync6 : 8;
	u8 gltim1 : 8; //8
	u8 gltim2 : 8;
	u8 gltim3 : 8;
	u8 gltim4 : 8;
	u8 gltim5 : 8;
	u8 gltim6 : 4;
	u8 resv1 : 4;
} EXT_SYNC_T_H_Frame_t;

/*88 bit little filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 addr1 : 8;
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 3;
	u8 len1 : 5; //8
	u8 len2 : 3;
	u8 size : 3;
	u8 bust : 2; //8
	u8 resv : 8;
} DDR_AW_L_Frame_t;

/*88 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 4;
	u8 addr1 : 4; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 7;
	u8 len1 : 1; //8
	u8 len2 : 7;
	u8 size1 : 1; //8
	u8 size2 : 2;
	u8 bust : 2;
	u8 resv1 : 4; //8
	u8 resv2 : 8;
} DDR_AW_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 addr1 : 8;
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 3;
	u8 len1 : 5; //8, 67bit
	u8 len2 : 3;
	u8 size : 3;
	u8 bust : 2; //8
	u8 resv : 8;
} DDR_AR_L_Frame_t;

/*88 bit high filed bit0*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 4;
	u8 addr1 : 4; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 7;
	u8 len1 : 1; //8
	u8 len2 : 7;
	u8 size1 : 1; //8
	u8 size2 : 2;
	u8 bust : 2;
	u8 resv1 : 4; //8
	u8 resv2 : 8;
} DDR_AR_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 bresp : 2;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} DDR_B_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 4;
	u8 bresp : 2;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
} DDR_B_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 narrow : 1;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
	u8 resv3 : 8;
} DDR_W_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 narrow : 1;
	u8 resv1 : 2;
	u8 resv2 : 4; //8
	u8 resv3 : 8; //8
} DDR_W_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 id1 : 3; //8
	u8 id2 : 7;
	u8 rresp1 : 1; //8
	u8 rresp2 : 1;
	u8 last : 1;
	u8 resv : 6; //8
} DDR_R_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 id1 : 7; //8
	u8 id2 : 3;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv1 : 2; //8
} DDR_R_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 6;
	u8 addr1 : 2; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 6;
	u8 len1 : 2; //8
	u8 len2 : 6;
	u8 size1 : 2; //8
	u8 size2 : 1;
	u8 bust : 2;
	u8 resv1 : 5; //8
} GPU_NPU_USB_AW_L_Frame_t;

/* 88 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 2;
	u8 addr1 : 6; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 2;
	u8 len1 : 6; //8
	u8 len2 : 2;
	u8 size : 3;
	u8 bust : 2;
	u8 resv1 : 1; //8
	u8 resv2 : 8;
} GPU_NPU_USB_AW_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 6;
	u8 addr1 : 2; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 6;
	u8 len1 : 2; //8
	u8 len2 : 6;
	u8 size1 : 2; //8
	u8 size2 : 1;
	u8 bust : 2;
	u8 resv : 5; //8
} GPU_NPU_USB_AR_L_Frame_t;

/*88 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 2;
	u8 addr1 : 6; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 2;
	u8 len1 : 6; //8
	u8 len2 : 2;
	u8 size : 3;
	u8 bust : 2;
	u8 resv1 : 1; //8
	u8 resv2 : 8; //8
} GPU_NPU_USB_AR_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 6;
	u8 bresp : 2; //8
	u8 resv1 : 8;
	u8 resv2 : 8;
} GPU_NPU_USB_B_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 2;
	u8 bresp : 2;
	u8 resv1 : 4; //8
	u8 resv2 : 8;
} GPU_NPU_USB_B_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 narrow : 1;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
	u8 resv3 : 8;
} GPU_NPU_USB_W_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 narrow : 1;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} GPU_NPU_USB_W_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 id1 : 3; //8
	u8 id2 : 5;
	u8 rresp : 2;
	u8 last : 1; //8
	u8 resv : 8; //8
} GPU_NPU_USB_R_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 id1 : 7; //8
	u8 id2 : 1;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv1 : 4; //8
} GPU_NPU_USB_R_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 7;
	u8 addr1 : 1; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 7;
	u8 len1 : 1; //8
	u8 len2 : 7;
	u8 size1 : 1; //8
	u8 size2 : 2;
	u8 bust : 2;
	u8 resv : 4; //8
} VO_PERI_AW_L_Frame_t;

/*88 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 3;
	u8 addr1 : 5; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 3;
	u8 len1 : 5; //8
	u8 len2 : 3;
	u8 size : 3;
	u8 bust : 2; //8
	u8 resv1 : 8;
} VO_PERI_AW_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 7;
	u8 addr1 : 1; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 7;
	u8 len1 : 1; //8
	u8 len2 : 7;
	u8 size1 : 1; //8
	u8 size2 : 2;
	u8 bust : 2;
	u8 resv : 4; //8
} VO_PERI_AR_L_Frame_t;

/*88 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 3;
	u8 addr1 : 5; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 3;
	u8 len1 : 5; //8
	u8 len2 : 3;
	u8 size : 3;
	u8 bust : 2; //8
	u8 resv1 : 8; //8
} VO_PERI_AR_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 7;
	u8 bresp1 : 1; //8
	u8 bresp2 : 1;
	u8 resv1 : 7; //8
	u8 resv2 : 8;
} VO_PERI_B_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 3;
	u8 bresp : 2;
	u8 resv1 : 3; //8
	u8 resv2 : 8;
} VO_PERI_B_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 narrow : 1;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
	u8 resv3 : 8;
} VO_PERI_W_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 narrow : 1;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} VO_PERI_W_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 id1 : 3; //8
	u8 id2 : 6;
	u8 rresp : 2; //8
	u8 last : 1;
	u8 resv : 7; //8
} VO_PERI_R_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 id1 : 7; //8
	u8 id2 : 2;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv1 : 3; //8
} VO_PERI_R_H_Frame_t;

/*88 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 id3 : 1;
	u8 addr1 : 7; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 1;
	u8 len1 : 7; //8
	u8 len2 : 1;
	u8 size : 3;
	u8 bust : 2;
	u8 resv : 2; //8
} VI_VP_AW_L_Frame_t;

/*88 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 5;
	u8 addr1 : 3; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 5;
	u8 len1 : 3; //8
	u8 len2 : 5;
	u8 size : 3; //8
	u8 bust : 2;
	u8 resv1 : 6; //8
} VI_VP_AW_H_Frame_t;

/*88 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 id3 : 1;
	u8 addr1 : 7; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 1;
	u8 len1 : 7; //8
	u8 len2 : 1;
	u8 size : 3;
	u8 bust : 2;
	u8 resv : 2; //8
} VI_VP_AR_L_Frame_t;

/*88 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 5;
	u8 addr1 : 3; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 5;
	u8 len1 : 3; //8
	u8 len2 : 5;
	u8 size : 3; //8
	u8 bust : 2;
	u8 resv1 : 6; //8
} VI_VP_AR_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 id3 : 1;
	u8 bresp : 2;
	u8 resv1 : 5; //8
	u8 resv2 : 8;
} VI_VP_B_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 5;
	u8 bresp : 2;
	u8 resv1 : 1; //8
	u8 resv2 : 8;
} VI_VP_B_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 narrow : 1;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
	u8 resv3 : 8;
} VI_VP_W_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 narrow : 1;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} VI_VP_W_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 id1 : 3; //8
	u8 id2 : 8;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv : 5; //8
} VI_VP_R_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 id1 : 7; //8
	u8 id2 : 4;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv1 : 1; //8
} VI_VP_R_H_Frame_t;

/*88 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 id3 : 2;
	u8 addr1 : 6; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 2;
	u8 len1 : 6; //8
	u8 len2 : 2;
	u8 size : 3;
	u8 bust : 2;
	u8 resv : 1; //8
} PCIE_AW_L_Frame_t;

/*88 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 6;
	u8 addr1 : 2; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 6;
	u8 len1 : 2; //8
	u8 len2 : 6;
	u8 size1 : 2; //8
	u8 size2 : 1;
	u8 bust : 2;
	u8 resv1 : 5; //8
} PCIE_AW_H_Frame_t;

/*88 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 id3 : 2;
	u8 addr1 : 6; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 2;
	u8 len1 : 6; //8
	u8 len2 : 2;
	u8 size : 3;
	u8 bust : 2;
	u8 resv : 1; //8
} PCIE_AR_L_Frame_t;

/*88 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 6;
	u8 addr1 : 2; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 6;
	u8 len1 : 2; //8
	u8 len2 : 6;
	u8 size1 : 2; //8
	u8 size2 : 1;
	u8 bust : 2;
	u8 resv1 : 5; //8
} PCIE_AR_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 id3 : 2;
	u8 bresp : 2;
	u8 resv1 : 4; //8
	u8 resv2 : 8;
} PCIE_B_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 6;
	u8 bresp : 2; //8
	u8 resv1 : 8;
} PCIE_B_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 narrow : 1;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
	u8 resv3 : 8;
} PCIE_W_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 narrow : 1;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} PCIE_W_H_Frame_t;

/*44 bit low filed bit0 */
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 id1 : 3; //8
	u8 id2 : 8;
	u8 id3 : 1;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv : 4; //8
} PCIE_R_L_Frame_t;

/*44 bit high filed bit4 */
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 id1 : 7; //8
	u8 id2 : 5;
	u8 rresp : 2;
	u8 last : 1; //8
} PCIE_R_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 addr1 : 8;
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 len : 8;
	u8 size : 3;
	u8 bust : 2;
	u8 resv : 3; //8
} D2D_AW_L_Frame_t;

/*88 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 4;
	u8 addr1 : 4; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 4;
	u8 len1 : 4; //8
	u8 len2 : 4;
	u8 size : 3;
	u8 bust1 : 1; //8
	u8 brst2 : 1;
	u8 resv1 : 7; //8
} D2D_AW_H_Frame_t;

/*88 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 addr1 : 8;
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 len : 8;
	u8 size : 3;
	u8 bust : 2;
	u8 resv : 3; //8
} D2D_AR_L_Frame_t;

/*88 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 4;
	u8 addr1 : 4; //8
	u8 addr2 : 8;
	u8 addr3 : 8;
	u8 addr4 : 8;
	u8 addr5 : 8;
	u8 addr6 : 4;
	u8 len1 : 4; //8
	u8 len2 : 4;
	u8 size : 3;
	u8 bust1 : 1; //8
	u8 brst2 : 1;
	u8 resv1 : 7; //8
} D2D_AR_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 lat1 : 1; //8
	u8 lat2 : 6;
	u8 id1 : 2; //8
	u8 id2 : 8;
	u8 bresp : 2;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} D2D_B_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 lat1 : 5; //8
	u8 lat2 : 2;
	u8 id1 : 6; //8
	u8 id2 : 4;
	u8 bresp : 2;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
} D2D_B_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 narrow : 1;
	u8 resv1 : 2; //8
	u8 resv2 : 8;
	u8 resv3 : 8;
} D2D_W_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 narrow : 1;
	u8 resv1 : 6; //8
	u8 resv2 : 8;
} D2D_W_H_Frame_t;

/*44 bit low filed bit0*/
typedef struct {
	u8 type : 3;
	u8 tim1 : 5; //8
	u8 tim2 : 7;
	u8 vr_lat1 : 1; //8
	u8 vr_lat2 : 6;
	u8 rl_lat1 : 2; //8
	u8 rl_lat2 : 5;
	u8 id1 : 3; //8
	u8 id2 : 7;
	u8 rresp1 : 1; //8
	u8 rresp2 : 1;
	u8 last : 1;
	u8 resv : 6; //8
} D2D_R_L_Frame_t;

/*44 bit high filed bit4*/
typedef struct {
	u8 resv : 4;
	u8 type : 3;
	u8 tim1 : 1; //8
	u8 tim2 : 8;
	u8 tim3 : 3;
	u8 vr_lat1 : 5; //8
	u8 vr_lat2 : 2;
	u8 rl_lat1 : 6; //8
	u8 rl_lat2 : 1;
	u8 id1 : 7; //8
	u8 id2 : 3;
	u8 rresp : 2;
	u8 last : 1;
	u8 resv1 : 2; //8
} D2D_R_H_Frame_t;
#pragma pack()

#endif
