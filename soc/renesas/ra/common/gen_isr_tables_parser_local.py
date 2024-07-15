#!/usr/bin/env python3
#
# Copyright (c) 2017 Intel Corporation
# Copyright (c) 2018 Foundries.io
# Copyright (c) 2023 Nordic Semiconductor NA
# Copyright (c) 2024 TOKITA Hiroshi
#
# SPDX-License-Identifier: Apache-2.0
#

import struct

class gen_isr_parser:
    source_header = """
/* AUTO-GENERATED by gen_isr_tables.py, do not edit! */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sw_isr_table.h>
#include <zephyr/arch/cpu.h>

"""

    shared_isr_table_header = """

/* For this parser to work, we have to be sure that shared interrupts table entry
 * and the normal isr table entry have exactly the same layout
 */
BUILD_ASSERT(sizeof(struct _isr_table_entry)
             ==
             sizeof(struct z_shared_isr_table_entry),
             "Shared ISR and ISR table entries layout do not match");
BUILD_ASSERT(offsetof(struct _isr_table_entry, arg)
             ==
             offsetof(struct z_shared_isr_table_entry, arg),
             "Shared ISR and ISR table entries layout do not match");
BUILD_ASSERT(offsetof(struct _isr_table_entry, isr)
             ==
             offsetof(struct z_shared_isr_table_entry, isr),
             "Shared ISR and ISR table entries layout do not match");

"""

    def __init__(self, intlist_data, config, log):
        """Initialize the parser.

        The function prepares parser to work.
        Parameters:
        - intlist_data: The binnary data from intlist section
        - config: The configuration object
        - log: The logging object, has to have error and debug methods
        """
        self.__config = config
        self.__log = log
        intlist = self.__read_intlist(intlist_data)
        self.__vt, self.__swt, self.__nv, header = self.__parse_intlist(intlist)
        self.__swi_table_entry_size = header["swi_table_entry_size"]
        self.__shared_isr_table_entry_size = header["shared_isr_table_entry_size"]
        self.__shared_isr_client_num_offset = header["shared_isr_client_num_offset"]

    def __read_intlist(self, intlist_data):
        """read an intList section from the elf file.
        This is version 2 of a header created by include/zephyr/linker/intlist.ld:

        struct {
            uint32_t num_vectors; <- typically CONFIG_NUM_IRQS
            uint8_t stream[];     <- the stream with the interrupt data
        };

        The stream is contained from variable length records in a form:

        struct _isr_list_sname {
           /** IRQ line number */
           int32_t irq;
           /** Flags for this IRQ, see ISR_FLAG_* definitions */
           int32_t flags;
           /** The section name */
           const char sname[];
        };

        The flexible array member here (sname) contains the name of the section where the structure
        with interrupt data is located.
        It is always Null-terminated string thus we have to search through the input data for the
        structure end.

        """
        intlist = {}
        prefix = self.__config.endian_prefix()

         # Extract header and the rest of the data
        intlist_header_fmt = prefix + "IIIII"
        header_sz = struct.calcsize(intlist_header_fmt)
        header_raw = struct.unpack_from(intlist_header_fmt, intlist_data, 0)
        self.__log.debug(str(header_raw))

        intlist["num_vectors"]                  = header_raw[0]
        intlist["offset"]                       = header_raw[1]
        intlist["swi_table_entry_size"]         = header_raw[2]
        intlist["shared_isr_table_entry_size"]  = header_raw[3]
        intlist["shared_isr_client_num_offset"] = header_raw[4]

        intdata = intlist_data[header_sz:]

        # Extract information about interrupts
        intlist_entry_fmt = prefix + "ii"
        entry_sz = struct.calcsize(intlist_entry_fmt)
        intlist["interrupts"] = []

        while len(intdata) > entry_sz:
            entry_raw = struct.unpack_from(intlist_entry_fmt, intdata, 0)
            intdata = intdata[entry_sz:]
            null_idx = intdata.find(0)
            if null_idx < 0:
                self.__log.error("Cannot find sname null termination at IRQ{}".format(entry_raw[0]))
            bname = intdata[:null_idx]
            # Next structure starts with 4B alignment
            next_idx = null_idx + 1
            next_idx = (next_idx + 3) & ~3
            intdata = intdata[next_idx:]
            sname = bname.decode()
            intlist["interrupts"].append([entry_raw[0], entry_raw[1], sname])
            self.__log.debug("Unpacked IRQ{}, flags: {}, sname: \"{}\"\n".format(
                entry_raw[0], entry_raw[1], sname))

        # If any data left at the end - it has to be all the way 0 - this is just a check
        if (len(intdata) and not all([d == 0 for d in intdata])):
            self.__log.error("Non-zero data found at the end of the intList data.\n")

        self.__log.debug("Configured interrupt routing with linker")
        self.__log.debug("irq flags sname")
        self.__log.debug("--------------------------")

        for irq in intlist["interrupts"]:
            self.__log.debug("{0:<3} {1:<5} {2}".format(
                hex(irq[0]), irq[1], irq[2]))

        return intlist

    def __parse_intlist(self, intlist):
        """All the intlist data are parsed into swt and vt arrays.

        The vt array is prepared for hardware interrupt table.
        Every entry in the selected position would contain None or the name of the function pointer
        (address or string).

        The swt is a little more complex. At every position it would contain an array of parameter and
        function pointer pairs. If CONFIG_SHARED_INTERRUPTS is enabled there may be more than 1 entry.
        If empty array is placed on selected position - it means that the application does not implement
        this interrupt.

        Parameters:
        - intlist: The preprocessed list of intlist section content (see read_intlist)

        Return:
        vt, swt - parsed vt and swt arrays (see function description above)
        """
        nvec = intlist["num_vectors"]
        offset = intlist["offset"]
        header = {
            "swi_table_entry_size":         intlist["swi_table_entry_size"],
            "shared_isr_table_entry_size":  intlist["shared_isr_table_entry_size"],
            "shared_isr_client_num_offset": intlist["shared_isr_client_num_offset"]
        }

        if nvec > pow(2, 15):
            raise ValueError('nvec is too large, check endianness.')

        self.__log.debug('offset is ' + str(offset))
        self.__log.debug('num_vectors is ' + str(nvec))

        # Set default entries in both tables
        if not(self.__config.args.sw_isr_table or self.__config.args.vector_table):
            self.__log.error("one or both of -s or -V needs to be specified on command line")
        if self.__config.args.vector_table:
            vt = [None for i in range(self.__config.interrupt_id_count)]
        else:
            vt = None
        if self.__config.args.sw_isr_table:
            swt = [[] for i in range(self.__config.interrupt_id_count)]
        else:
            swt = None

        # Process intlist and write to the tables created
        for irq, flags, sname in intlist["interrupts"]:
            if self.__config.test_isr_direct(flags):
                if not 0 <= irq - offset < len(vt):
                    self.__log.error("IRQ %d (offset=%d) exceeds the maximum of %d" %
                          (irq - offset, offset, len(vt) - 1))
                vt[irq - offset] = sname
            else:
                # Regular interrupt
                if not swt:
                    self.__log.error("Regular Interrupt %d declared with section name %s "
                                     "but no SW ISR_TABLE in use"
                                     % (irq, sname))

                table_index = self.__config.get_swt_table_index(offset, irq)

                if not 0 <= table_index < len(swt):
                    self.__log.error("IRQ %d (offset=%d) exceeds the maximum of %d" %
                                     (table_index, offset, len(swt) - 1))
                # Check if the given section name does not repeat outside of current interrupt
                for i in range(nvec):
                    if i == irq:
                        continue
                    if sname in swt[i]:
                        self.__log.error(("Attempting to register the same section name \"{}\"for" +
                                          "different interrupts: {} and {}").format(sname, i, irq))
                if self.__config.check_shared_interrupts():
                    lst = swt[table_index]
                    if len(lst) >= self.__config.get_sym("CONFIG_SHARED_IRQ_MAX_NUM_CLIENTS"):
                        self.__log.error(f"Reached shared interrupt client limit. Maybe increase"
                                         + f" CONFIG_SHARED_IRQ_MAX_NUM_CLIENTS?")
                else:
                    if len(swt[table_index]) > 0:
                        self.__log.error(f"multiple registrations at table_index {table_index} for irq {irq} (0x{irq:x})"
                                         + f"\nExisting section {swt[table_index]}, new section {sname}"
                                         + "\nHas IRQ_CONNECT or IRQ_DIRECT_CONNECT accidentally been invoked on the same irq multiple times?"
                        )
                swt[table_index].append(sname)

        return vt, swt, nvec, header

    @staticmethod
    def __irq_spurious_section(irq):
        return '.irq_spurious.0x{:x}'.format(irq)

    @staticmethod
    def __isr_generated_section(irq):
        return '.isr_generated.0x{:x}'.format(irq)

    @staticmethod
    def __shared_entry_section(irq, ent):
        return '.isr_shared.0x{:x}_0x{:x}'.format(irq, ent)

    @staticmethod
    def __shared_client_num_section(irq):
        return '.isr_shared.0x{:x}_client_num'.format(irq)

    def __isr_spurious_entry(self, irq):
        return '_Z_ISR_TABLE_ENTRY({irq}, {func}, NULL, "{sect}");'.format(
                irq = irq,
                func = self.__config.swt_spurious_handler,
                sect = self.__isr_generated_section(irq)
            )

    def __isr_shared_entry(self, irq):
        return '_Z_ISR_TABLE_ENTRY({irq}, {func}, {arg}, "{sect}");'.format(
                irq = irq,
                arg = '&{}[{}]'.format(self.__config.shared_array_name, irq),
                func = self.__config.swt_shared_handler,
                sect = self.__isr_generated_section(irq)
            )

    def __irq_spurious_entry(self, irq):
        return '_Z_ISR_DIRECT_TABLE_ENTRY({irq}, {func}, "{sect}");'.format(
                irq = irq,
                func = self.__config.vt_default_handler,
                sect = self.__irq_spurious_section(irq)
            )

    def __write_isr_handlers(self, fp):
        for i in range(self.__nv):
            if len(self.__aligned_swt()[i]) <= 0:
                fp.write(self.__isr_spurious_entry(i) + '\n')
            elif len(self.__aligned_swt()[i]) > 1:
                # Connect to shared handlers
                fp.write(self.__isr_shared_entry(i) + '\n')
            else:
                fp.write('/* ISR: {} implemented in app in "{}" section. */\n'.format(
                         i, self.__aligned_swt()[i][0]))

    def __write_irq_handlers(self, fp):
        for i in range(self.__nv):
            if self.__aligned_vt()[i] is None:
                fp.write(self.__irq_spurious_entry(i) + '\n')
            else:
                fp.write('/* ISR: {} implemented in app. */\n'.format(i))

    def __write_shared_handlers(self, fp):
        fp.write("extern struct z_shared_isr_table_entry "
                "{}[{}];\n".format(self.__config.shared_array_name, self.__nv))

        shared_cnt = self.__config.get_sym('CONFIG_SHARED_IRQ_MAX_NUM_CLIENTS')
        for i in range(self.__nv):
            swt_len = len(self.__aligned_swt()[i])
            for j in range(shared_cnt):
                if (swt_len <= 1) or (swt_len <= j):
                    # Add all unused entry
                    fp.write('static Z_DECL_ALIGN(struct _isr_table_entry)\n' +
                             '\tZ_GENERIC_SECTION({})\n'.format(self.__shared_entry_section(i, j)) +
                             '\t__used isr_shared_empty_entry_0x{:x}_0x{:x} = {{\n'.format(i, j) +
                             '\t\t.arg = (const void *)NULL,\n' +
                             '\t\t.isr = (void (*)(const void *))(void *)0\n' +
                             '};\n'
                    )
                else:
                    # Add information about entry implemented by application
                    fp.write('/* Shared isr {} entry {} implemented in "{}" section*/\n'.format(
                             i, j, self.__aligned_swt()[i][j]))

            # Add information about clients count
            fp.write(('static size_t Z_GENERIC_SECTION({}) __used\n' +
                      'isr_shared_client_num_0x{:x} = {};\n\n').format(
                         self.__shared_client_num_section(i),
                         i,
                         0 if swt_len < 2 else swt_len)
            )

    def __aligned_vt(self):
        """Returns vt with non-None entries aligned in front.
        """
        vt = [v for v in self.__vt if not v is None]
        vt.extend([None] * (self.__nv- len(vt)))
        return vt

    def __aligned_swt(self):
        """Returns swt with non-None entries aligned in front.
        Empty entries equal to the number of vt entries are inserted at the beginning.
        """
        swt = [[] for v in self.__vt if not v is None]
        swt.extend([sw for sw in self.__swt if not sw == []])
        swt.extend([[]] * (self.__nv - len(swt)))
        return swt

    def __write_intr_id_table(self, fp):
        vt_len = len([v for v in self.__vt if not v is None]) if self.__vt else 0
        swt_len = len([sw for sw in self.__swt if not sw == []]) if self.__swt else 0

        if (vt_len + swt_len) > self.__nv:
            self.__log.error("Too many ISRs are defined.")

        fp.write("const ra_intr_id_t __intr_id_table[RA_INTR_ID_COUNT] = {\n")

        aligned_vt = self.__aligned_vt()
        aligned_swt = self.__aligned_swt()

        for i in range(self.__config.interrupt_id_count):
            v = self.__vt[i]
            sw  = self.__swt[i]
            if v is not None and v in self.__aligned_vt():
                fp.write("\t            {1:>6}, /* {0:>3} */\n".format(i, aligned_vt.index(v)))
            elif sw != [] and sw in self.__aligned_swt():
                fp.write("\t            {1:>6}, /* {0:>3} */\n".format(i, aligned_swt.index(sw)))
            else:
                fp.write("\tRA_INVALID_INTR_ID, /* {0:>3} */\n".format(i))

        fp.write("};\n")

    def write_source(self, fp):
        fp.write(self.source_header)

        if self.__vt:
            self.__write_irq_handlers(fp)

        if not self.__swt:
            return

        if self.__config.check_shared_interrupts():
            self.__write_shared_handlers(fp)

        self.__write_isr_handlers(fp)

        self.__write_intr_id_table(fp)

    def __write_linker_irq(self, fp):
        fp.write('{} = .;\n'.format(self.__config.irq_vector_array_name))
        for i in range(self.__nv):
            if self.__aligned_vt()[i] is None:
                sname = self.__irq_spurious_section(i)
            else:
                sname = self.__aligned_vt()[i]
            fp.write('KEEP(*("{}"))\n'.format(sname))

    def __write_linker_shared(self, fp):
        fp.write(". = ALIGN({});\n".format(self.__shared_isr_table_entry_size))
        fp.write('{} = .;\n'.format(self.__config.shared_array_name))
        shared_cnt = self.__config.get_sym('CONFIG_SHARED_IRQ_MAX_NUM_CLIENTS')
        client_num_pads = self.__shared_isr_client_num_offset - \
            shared_cnt * self.__swi_table_entry_size
        if client_num_pads < 0:
            self.__log.error("Invalid __shared_isr_client_num_offset header value")
        for i in range(self.__nv):
            swt_len = len(self.__aligned_swt()[i])
            # Add all entries
            for j in range(shared_cnt):
                if (swt_len <= 1) or (swt_len <= j):
                    fp.write('KEEP(*("{}"))\n'.format(self.__shared_entry_section(i, j)))
                else:
                    sname = self.__aligned_swt()[i][j]
                    if (j != 0) and (sname in self.__aligned_swt()[i][0:j]):
                        fp.write('/* Repetition of "{}" section */\n'.format(sname))
                    else:
                        fp.write('KEEP(*("{}"))\n'.format(sname))
            fp.write('. = . + {};\n'.format(client_num_pads))
            fp.write('KEEP(*("{}"))\n'.format(self.__shared_client_num_section(i)))
            fp.write(". = ALIGN({});\n".format(self.__shared_isr_table_entry_size))

    def __write_linker_isr(self, fp):
        fp.write(". = ALIGN({});\n".format(self.__swi_table_entry_size))
        fp.write('{} = .;\n'.format(self.__config.sw_isr_array_name))
        for i in range(self.__nv):
            if (len(self.__aligned_swt()[i])) == 1:
                sname = self.__aligned_swt()[i][0]
            else:
                sname = self.__isr_generated_section(i)
            fp.write('KEEP(*("{}"))\n'.format(sname))

    def write_linker_vt(self, fp):
        if self.__vt:
            self.__write_linker_irq(fp)

    def write_linker_swi(self, fp):
        if self.__swt:
            self.__write_linker_isr(fp)

            if self.__config.check_shared_interrupts():
                self.__write_linker_shared(fp)
