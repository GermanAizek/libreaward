## Table Tested Compability

Motherboard | Nuances and problems | Status
--- | --- | ---
GIGABYTE GA-P55-UD3L rev. 2.3 | After firmware and restart, stable work, but it was enough to go into BIOS Setup as after that it no longer started | Partially worked

## Recovery BIOS with Dual BIOS (M_BIOS and B_BIOS)

1. Open the computer case
2. We find on the motherboard a chip marked on the board as M_BIOS (This is SPI flash 8-pin)
3. Take tweezers, and when you turn on the computer, literally after 0,5-1-2 seconds,
we shorten the pins of the microcircuit 1 and 8 once (a visual screenshot of which legs to shorten is below. Circled in red)

<img src="https://github.com/user-attachments/assets/720c741a-0e5e-4e04-876a-598fa40a9cff" alt="LibreAward Recovery Dual BIOS" width="27%" height="27%">  <img src="https://github.com/user-attachments/assets/efe256cf-be9c-487f-a77c-5363441dddac" alt="LibreAward Recovery Dual BIOS" width="25%" height="25%">


*IMPORTANT!!!*

Pay attention to the turquoise (on other motherboards there may be a different color) dot on the chip. This is a marker indicating the first pin, it will serve as a guide for you, so as not to close the wrong legs. Make sure that you are closing exactly M_BIOS and not B_BIOS. Otherwise, you will reset/damage the backup chip, then you will have to use CH341a programmer with IMSProg program.

If you short out SPI pins too early - and BIOS has not yet reached CRC calculate checksum stage, then we will get, in a few seconds, a permanent reboot without any results.

If it is too late, bios has already checked CRC firmware "successfully" and has been copied to "shadow" section. At same time, desired result, as rule, will also not be. The computer, when turned on again, will simply turn on and work "idle".

It is necessary to repeat the closure attempt with a slight "shift" in time, in one direction or another. Usually, this is no more than 10-20 attempts (it can just sequentially "iterate" over time intervals from zero seconds and beyond).
