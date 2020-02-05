# Davinci target configuration
include $(srctree)/techpack/audio/config/davinciauto.conf
export

# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE     += \
                -I$(srctree)/techpack/audio/include/uapi

# Use LINUXINCLUDE when you must reference the include/ directory.
# Needed to be compatible with the O= option
LINUXINCLUDE    += \
                -I$(srctree)/techpack/audio/include/uapi \
                -I$(srctree)/techpack/audio/include

# Include configuration defines
LINUXINCLUDE    += \
                -include $(srctree)/techpack/audio/config/davinciautoconf.h

obj-y += soc/
obj-y += dsp/
obj-y += ipc/
obj-y += asoc/
