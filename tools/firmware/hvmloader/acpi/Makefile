#
# Copyright (c) 2004, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place - Suite 330, Boston, MA 02111-1307 USA.
#

XEN_ROOT = $(CURDIR)/../../../..
include $(XEN_ROOT)/tools/firmware/Rules.mk

C_SRC = build.c dsdt_anycpu.c dsdt_15cpu.c static_tables.c dsdt_anycpu_qemu_xen.c
OBJS  = $(patsubst %.c,%.o,$(C_SRC))

CFLAGS += $(CFLAGS_xeninclude)

vpath iasl $(PATH)
vpath python $(PATH)

.DELETE_ON_ERROR:$(filter dsdt_%.c,$(C_SRC))

all: acpi.a

ssdt_s3.h ssdt_s4.h ssdt_pm.h ssdt_tpm.h: %.h: %.asl iasl
	iasl -vs -p $* -tc $<
	sed -e 's/AmlCode/$*/g' $*.hex >$@.tmp
	$(call move-if-changed,$@.tmp $@)
	rm -f $*.hex $*.aml

mk_dsdt: mk_dsdt.c
	$(HOSTCC) $(HOSTCFLAGS) $(CFLAGS_xeninclude) -o $@ mk_dsdt.c

dsdt_anycpu_qemu_xen.asl: dsdt.asl mk_dsdt
	awk 'NR > 1 {print s} {s=$$0}' $< > $@.tmp
	sed -i 's/AmlCode/dsdt_anycpu_qemu_xen/g' $@.tmp
	./mk_dsdt --dm-version qemu-xen >> $@.tmp
	sed -i 's/aml_ej0_name/dsdt_anycpu_qemu_xen_aml_ej0_name/g' $@.tmp
	sed -i 's/aml_adr_dword/dsdt_anycpu_qemu_xen_aml_adr_dword/g' $@.tmp
	$(call move-if-changed,$@.tmp $@)

# NB. awk invocation is a portable alternative to 'head -n -1'
dsdt_%cpu.asl: dsdt.asl mk_dsdt
	awk 'NR > 1 {print s} {s=$$0}' $< > $@.tmp
	sed -i 's/AmlCode/dsdt_$*cpu/g' $@.tmp
	./mk_dsdt --maxcpu $*  >> $@.tmp
	$(call move-if-changed,$@.tmp $@)

$(filter dsdt_%.c,$(C_SRC)): %.c: iasl python %.asl
	cpp -P $*.asl > $*.asl.i.orig
	python ../tools/acpi_extract_preprocess.py $*.asl.i.orig > $*.asl.i
	iasl -vs -l -tc -p $* $*.asl.i
	python ../tools/acpi_extract.py $*.lst > $@.tmp
	echo "int $*_len=sizeof($*);" >>$@.tmp
	if grep -q "$*_aml_ej0_name" $@.tmp; then echo "int $*_aml_ej0_name_len=sizeof($*_aml_ej0_name);" >>$@.tmp; fi
	if grep -q "$*_aml_adr_dword" $@.tmp; then echo "int $*_aml_adr_dword_len=sizeof($*_aml_adr_dword);" >>$@.tmp;fi
	$(call move-if-changed,$@.tmp $@)
	rm -f $*.aml $*.hex $*.asl.i.orig $*.asl.i $*.lst

iasl:
	@echo
	@echo "ACPI ASL compiler (iasl) is needed"
	@echo "Download and install Intel ACPI CA from"
	@echo "http://acpica.org/downloads/"
	@echo 
	@exit 1

python:
	@echo
	@echo "python is needed"
	@echo
	@exit 1

build.o: ssdt_s3.h ssdt_s4.h ssdt_pm.h ssdt_tpm.h

acpi.a: $(OBJS)
	$(AR) rc $@ $(OBJS)

clean:
	rm -rf *.a *.o $(IASL_VER) $(IASL_VER).tar.gz $(DEPS)
	rm -rf ssdt_*.h dsdt*.c *~ *.aml *.hex mk_dsdt dsdt_*.asl *.asl.i *.asl.i.orig *.lst *.tmp

install: all

-include $(DEPS)
