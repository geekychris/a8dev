NAME   := a8_3d
A8_SDK ?= ../../sdk
# a3d's tables and work arrays in LOWRAM ($0C00-$1FFF): the bench and its
# checks fill the 64K program area otherwise. A3D_FFILL: the fast path for
# polygons wholly on the screen (the checks draw through it; the clipped
# ones and the other games' tests cover the edge tables)
EXTRA_CFLAGS  := -DA3D_LOWTAB -DA3D_FFILL
EXTRA_ASFLAGS := -DA3D_LOWTAB -DA3D_FFILL
include a3d.mk
include $(A8_SDK)/project.mk
