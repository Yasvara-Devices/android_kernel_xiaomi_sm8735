# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_QRTR) += qrtr.o
qrtr-y	:= af_qrtr.o ns.o

obj-$(CONFIG_QRTR_SMD) += qrtr-smd.o
qrtr-smd-y	:= smd.o
obj-$(CONFIG_QRTR_TUN) += qrtr-tun.o
qrtr-tun-y	:= tun.o
obj-$(CONFIG_QRTR_MHI) += qrtr-mhi.o
qrtr-mhi-y	:= mhi.o

# --- KP 5.0 soc-repo additions ---
obj-$(CONFIG_QRTR_GUNYAH) += qrtr-gunyah.o
qrtr-gunyah-objs += gunyah.o
obj-$(CONFIG_QRTR_GENPOOL) += qrtr-genpool.o
qrtr-genpool-objs += genpool.o
qrtr-objs += af_qrtr.o ns.o
qrtr-mhi-objs += mhi.o
qrtr-smd-objs += smd.o
qrtr-tun-objs += tun.o
