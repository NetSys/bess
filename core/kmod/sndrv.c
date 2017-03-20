/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2014 Sangjin Han All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/* Dual BSD/GPL */

#include "sn_common.h"
#include "sn_kernel.h"

extern struct miscdevice sn_host_device;

static int __init sndrv_init(void)
{
	int ret;

	log_info("vport kernel module loaded\n");

	ret = misc_register(&sn_host_device);
	if (ret < 0) {
		log_err("misc_register() failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void __exit sndrv_exit(void)
{
	misc_deregister(&sn_host_device);
	log_info("kernel module unloaded\n");
}

module_init(sndrv_init);
module_exit(sndrv_exit);

MODULE_AUTHOR("Sangjin Han");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Paravirtualized device driver for BESS vport");
