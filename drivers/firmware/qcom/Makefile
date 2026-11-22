# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-$(CONFIG_QCOM_SCM)		+= qcom-scm.o
qcom-scm-objs += qcom_scm.o qcom_scm-smc.o qcom_scm-legacy.o
obj-$(CONFIG_QCOM_TZMEM)	+= qcom_tzmem.o
obj-$(CONFIG_QCOM_QSEECOM)	+= qcom_qseecom.o
obj-$(CONFIG_QCOM_QSEECOM_UEFISECAPP) += qcom_qseecom_uefisecapp.o

# --- KP 5.0 soc-repo additions ---
obj-y += pkvm-secure-memory-manager/
obj-y += pkvm-smc-filter/
obj-y += si_core/
qcom-scm-$(CONFIG_QTEE_SHM_BRIDGE) += qtee_shmbridge.o
qcom-scm-$(CONFIG_MSM_HAB) += qcom_scm_hab.o
ifeq ($(CONFIG_QCOM_SCM_SMCI),)
qcom-scm-y += qcom_scm-scm.o
endif
obj-$(CONFIG_QCOM_SCM_SMCI) += qcom_scm_smci.o
qcom_scm_smci-$(CONFIG_QCOM_SCM_SMCI) += qcom_scm-smcinvoke.o
