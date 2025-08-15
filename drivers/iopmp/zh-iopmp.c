#include <asm/sbi.h>
#include <asm/zh-iopmp.h>

static long iopmp_ctrl(uint32_t *device_ids, uint32_t count, int iopmp_ext_id, int fid )
{
	struct sbiret ret = {0};
	uint32_t *p_devices = device_ids;

	for (int i = 0; i < count; i+=5) {
		uint32_t devices[5] = {0};

		uint32_t send_count = min((count - i), (uint32_t)5);
		for (int i = 0; i < send_count; i++)
			devices[i] = *(p_devices + i);

		ret = sbi_ecall(iopmp_ext_id, fid, send_count, devices[0], devices[1], devices[2], devices[3], devices[4]);
		if (ret.error)
			break;

		p_devices += send_count;
	}

	return ret.error ? ret.error : ret.value;
}

/**
 * iompm_enable: enable iopmp config for domain
 * @device_ids: device id arrary

 * return: sbi_ecall result
 */
long iopmp_enable(uint32_t *device_ids, uint32_t count){
	return iopmp_ctrl(device_ids, count, SBI_EXT_CONFIG_IOPMP, SBI_EXT_CONFIG_IOPMP_ADD_RULE);
}

/**
 * iompm_disbale: disbale iopmp config for domain
 * @device_ids: device id arrary

 * return: sbi_ecall result
 */
long iopmp_disable(uint32_t *device_ids, uint32_t count)
{
	return iopmp_ctrl(device_ids, count, SBI_EXT_CONFIG_IOPMP, SBI_EXT_CONFIG_IOPMP_REMOVE_RULE);
}

EXPORT_SYMBOL(iopmp_disable);
EXPORT_SYMBOL(iopmp_enable);
