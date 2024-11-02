# SAM Coupé Scroller

This project attempts to implement a full-screen 50Hz scrolling title for the SAM Coupé. It is therefore possibly iterating towards a two-thirds screen 25Hz scrolling title for the SAM Coupé.

## Technical Background

The SAM Coupé is an 8-bit computer from the 1980s with a 6Mhz Z80 and a 24kb frame buffer which does not support any form of hardware scrolling, accelerated drawing or other adornments — though the buffer can be set to start at any 16kb boundary. Video RAM sits in the CPU's address space and is therefore subject to arbitrated access; in the native video modes the CPU may access RAM only once in every four cycles during the border and once in every eight cycles during the active display area.

Display is active on 192 lines out of 312, for 256 out of 384 cycles per line.

Per my arithmetic, that gives 120 lines where the CPU may access RAM 96 times a line plus 192 lines where the CPU may access RAM 32 times during the border plus 32 times during the active display for 64 accesses total. So there are a total of 120×96 + 192×64 = 23,808 available access windows per frame.

So it would be impossible to touch every one of the 24,576 bytes of the display in a single frame even if the CPU didn't also have its own instruction stream to fetch and somehow knew which colour to paint where of its own volition.

An approximate rule of thumb is that performant code in ideal conditions (no overdraw, no read-modify-write, etc) can update a quarter to a third of the display per frame.

## Achieving 'performant code'

A trite observsation is that the Z80 offers only one way to write to memory with automatic address advancement, and that's to use the stack pointer for graphics output.

This project runs with that precept and uses ahead-of-time compilation for all graphical assets in order to embed pixel data in the instruction stream. An attempt is made at better-than-dumb register allocation to reduce repetition of pixel data but somebody with a formal background in compilers could likely do a better job.

### No Stack

In this implementation the stack pointer is used _exclusively_ for rectangular graphics output as described above. Interrupts are disabled and DE is used as an ersatz link register for subroutine calls, which as a result are not reentrant.

For frame synchronisation the current state of the SAM's line interrupt is polled until a recurring target of 191 (i.e. the end of the final visible line) is hit.

## Minimising Draw Area

This implementation — like many before — attempts to use a tile map in order to approximate a minimial area of redraw and scrolls in one direction only; when making a tile-sized scroll step from starting at column n to starting at column n+1 the tile at (x, y) needs to be redrawn only if the tile at (x+1, y) is different.

A precomputed table of difference flags records the result of those comparisons out ahead of time.

Different back buffers are used for the current map offet in sub-tile increments. This permits scrolling in 2-pixel increments rather than in whole-tile increments.

Noting for completeness: in net this makes for a similar effect as ESI's Surprise demo, which seems to show an ever-increasing number of balls in motion but is merely plotting the front and maintaining a sequence of video buffers for the illusion of gaps between each ball and the one that follows it.

## Adding Sprites

Sprites are precompiled but don't use the stack pointer for output since it makes masking difficult. They mark a tile-map aligned grid of dirty flags so that they're erased automatically during the standard tile output routine.

Those dirty flags are ORd with the precomputed differences set when scrolling, but used in isolation when the screen is static.

### Net Corollary: 50Hz or bust

The SAM, like the Spectrum before it, signals its interrupts for a fixed amount of time then silently stops signalling. Since this program merely polls for the appropriate interrupt it can therefore only ever synchronise to the _next_ interrupt, with no idea of how many have fallen unobserved. The working hope is that game maps and sprite totals can be massaged as necessary to resolve areas of slowdown.

Also notable is that the use of different display buffers for different sub-tile scroll offsets means that there is no back buffer if the screen has not scrolled, there is only the one that is visible. It is therefore necessary that redrawing the number of tiles covered by all sprites that have moved plus drawing the new sprites be finished within the 120 lines that do not contain any pixels.

In practice I hope this will prove not to be too much of a constraint since those 120 lines total almost half of the available RAM access slots and there are further levers available if issues present such as not allowing all sprites to move on every frame.
