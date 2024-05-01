#!/usr/bin/env bash
# e2e/170-acpi-q35.sh - on q35 the kernel finds the RSDP
# the loader passed, walks the XSDT, parses the MADT (LAPIC, IOAPIC,
# overrides), the FADT (PM1a control, reset register, century, \_S5
# from the DSDT) and the MCFG, and PCI config space goes through ECAM.
# The boot then reaches the BLANK.ELF selftest over AHCI, whose
# controller was found and set up through ECAM.
# e2e-requires: acpi (the ACPI tables (RSDP, MADT, FADT, MCFG) of q35)
. "$(dirname "$0")/_lib.sh"

E2E_MACHINE=q35
boot_and_dump 30
expect_serial '^acpi: rsdp revision 2, xsdt [0-9]+ tables:.* FACP.* APIC.* MCFG.* DSDT$' "XSDT walked, FACP APIC MCFG and DSDT found"
expect_serial '^acpi: madt lapic 0x00000000fee00000, [1-9][0-9]* cpu, 1 ioapic, [1-9][0-9]* overrides' "MADT parsed"
expect_serial '^acpi: fadt pm1a_cnt 0x0000000000000604, reset 0x0000000000000cf9, century 50, _S5 0/0$' "FADT parsed, _S5 found in the DSDT"
expect_serial '^rtc: 20[0-9][0-9]-' "RTC read"
expect_serial '^pci: config access ECAM, base 0x00000000[0-9a-f]+, buses 0-255$' "PCI config through ECAM"
expect_no_serial 'bad checksum|bad RSDP|bad root table' "no table refused"
expect_stage_value 5 -gt 3 "PCI devices found through ECAM"
expect_serial '^e2e: user selftest done' "BLANK.ELF selftest over AHCI"
echo "PASS: e2e/170 acpi q35"
