# SPDX-License-Identifier: GPL-2.0
OBJECT_FILES_NON_STANDARD := y
CFLAGS_REMOVE_empty.o += $(CC_FLAGS_LTO)

hostprogs-always-y	+= modpost mk_elfconfig
always-y		+= empty.o

ifdef KBUILD_SCRIPTROOT
MKELFCONFIG = $(KBUILD_SCRIPTROOT)/scripts/mod/mk_elfconfig
else
MKELFCONFIG = $(obj)/mk_elfconfig
endif

modpost-objs	:= modpost.o file2alias.o sumversion.o

devicetable-offsets-file := devicetable-offsets.h

$(obj)/$(devicetable-offsets-file): $(obj)/devicetable-offsets.s FORCE
	$(call filechk,offsets,__DEVICETABLE_OFFSETS_H__)

targets += $(devicetable-offsets-file) devicetable-offsets.s

# dependencies on generated files need to be listed explicitly

$(obj)/modpost.o $(obj)/file2alias.o $(obj)/sumversion.o: $(obj)/elfconfig.h
$(obj)/file2alias.o: $(obj)/$(devicetable-offsets-file)

quiet_cmd_elfconfig = MKELF   $@
      cmd_elfconfig = $(MKELFCONFIG) < $< > $@

$(obj)/elfconfig.h: $(obj)/empty.o $(MKELFCONFIG) FORCE
	$(call if_changed,elfconfig)

targets += elfconfig.h
