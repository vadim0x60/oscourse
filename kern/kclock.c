/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <kern/kclock.h>

// Stupid I/O delay routine necessitated by historical PC design flaws
static void
delay(void)
{
	inb(0x84);
	inb(0x84);
	inb(0x84);
	inb(0x84);
}

uint8_t
read_cmos(uint8_t reg)
{
	outb(IO_RTC_CMND, reg);
	delay();
	return inb(IO_RTC_DATA);
}

void
write_cmos(uint8_t reg, uint8_t data)
{
	outb(IO_RTC_CMND, reg);
	delay();
	outb(IO_RTC_DATA, data);
}

void
rtc_init(void)
{
	int divider = 15;

	nmi_disable();
	write_cmos(0x8B, read_cmos(0x8B) | RTC_PIE); // Enable periodic interrupts
	write_cmos(0x8A, (read_cmos(0x8A) & 0xF0) | divider);
	nmi_enable();
}

uint8_t
rtc_check_status(void)
{
	return read_cmos(0x8C);
}

unsigned
mc146818_read(unsigned reg)
{
	outb(IO_RTC_CMND, reg);
	return inb(IO_RTC_DATA);
}

void
mc146818_write(unsigned reg, unsigned datum)
{
	outb(IO_RTC_CMND, reg);
	outb(IO_RTC_DATA, datum);
}

