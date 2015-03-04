include $(REP_DIR)/lib/mk/virtualbox-common.inc

SRC_CC += VMM/VMMR3/VM.cpp
SRC_CC += VMM/VMMAll/VMAll.cpp
SRC_CC += VMM/VMMAll/VMMAll.cpp
SRC_CC += VMM/VMMR3/VMM.cpp

SRC_CC += VMM/VMMR3/STAM.cpp

SRC_CC += VMM/VMMR3/SSM.cpp

SRC_CC += VMM/VMMR3/PDM.cpp
SRC_CC += VMM/VMMR3/PDMBlkCache.cpp
SRC_CC += VMM/VMMR3/PDMDevice.cpp
SRC_CC += VMM/VMMR3/PDMQueue.cpp
SRC_CC += VMM/VMMR3/PDMCritSect.cpp
SRC_CC += VMM/VMMAll/PDMAll.cpp
SRC_CC += VMM/VMMAll/PDMAllQueue.cpp
SRC_CC += VMM/VMMAll/PDMAllCritSect.cpp
SRC_CC += VMM/VMMAll/PDMAllCritSectRw.cpp

SRC_CC += VMM/VMMR3/TM.cpp
SRC_CC += VMM/VMMAll/TMAll.cpp
SRC_CC += VMM/VMMAll/TMAllVirtual.cpp
SRC_CC += VMM/VMMAll/TMAllReal.cpp
SRC_CC += VMM/VMMAll/TMAllCpu.cpp
SRC_CC += VMM/VMMAll/TRPMAll.cpp

SRC_CC += VMM/VMMR3/CFGM.cpp

SRC_CC += VMM/VMMR3/PDMDevHlp.cpp
SRC_CC += VMM/VMMR3/PDMDevMiscHlp.cpp
SRC_CC += VMM/VMMR3/PDMDriver.cpp
SRC_CC += VMM/VMMR3/PDMThread.cpp

SRC_CC += VMM/VMMAll/CPUMAllMsrs.cpp
SRC_CC += VMM/VMMAll/CPUMAllRegs.cpp

SRC_CC += VMM/VMMR3/VMEmt.cpp
SRC_CC += VMM/VMMR3/VMReq.cpp

SRC_CC += VMM/VMMAll/DBGFAll.cpp
SRC_CC += VMM/VMMR3/DBGFInfo.cpp

SRC_CC += VMM/VMMR3/CPUM.cpp
SRC_CC += VMM/VMMR3/CPUMR3CpuId.cpp
SRC_CC += VMM/VMMR3/CPUMR3Db.cpp

SRC_CC += VMM/VMMAll/EMAll.cpp
SRC_CC += VMM/VMMR3/EM.cpp
SRC_CC += VMM/VMMR3/EMHM.cpp

SRC_CC += VMM/VMMR3/TRPM.cpp
SRC_CC += VMM/VMMAll/SELMAll.cpp

SRC_CC += VMM/VMMAll/REMAll.cpp

SRC_CC += VMM/VMMR3/VMMGuruMeditation.cpp

CC_OPT += -DVBOX_IN_VMM

# definitions needed by SSM.cpp
CC_OPT += -DKBUILD_TYPE=\"debug\" \
          -DKBUILD_TARGET=\"genode\" \
          -DKBUILD_TARGET_ARCH=\"x86\"

# definitions needed by VMMAll.cpp
CC_OPT += -DVBOX_SVN_REV=~0

INC_DIR += $(VBOX_DIR)/VMM/include

CC_WARN += -Wno-unused-but-set-variable
