#!/usr/bin/env bash
# e2e/171-acpi-pc.sh - on pc (i440fx) the ACPI tables are
# parsed the same way, there is no MCFG, so PCI config space stays on
# ports 0xCF8/0xCFC, and the FADT has no reset register.
. "$(dirname "$0")/_lib.sh"

E2E_MACHINE=pc
boot_and_dump 30
expect_serial '^acpi: rsdp revision 2, xsdt [0-9]+ tables:.* FACP.* APIC.* DSDT$' "XSDT walked, FACP APIC and DSDT found"
expect_no_serial '^acpi: rsdp.* MCFG' "no MCFG on pc"
expect_serial '^acpi: madt lapic 0x00000000fee00000, [1-9][0-9]* cpu, 1 ioapic, [1-9][0-9]* overrides, 8259 present$' "MADT parsed"
expect_serial '^acpi: fadt pm1a_cnt 0x000000000000b004, reset none, century 50, _S5 0/0$' "FADT parsed, _S5 found"
expect_serial '^pci: no MCFG, config access ports 0xcf8/0xcfc$' "PCI config through the ports"
expect_stage_value 5 -gt 3 "PCI devices found"
expect_stage_value 23 -eq 1 "IDE controller found"
expect_serial '^e2e: user selftest done' "BLANK.ELF selftest over IDE"
echo "PASS: e2e/171 acpi pc"
