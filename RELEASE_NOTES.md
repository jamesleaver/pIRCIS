# pIRCIS 1.4.1

A small fix to the scroll bars on the RUN and EDIT pages.

## The edge bars sit on cell boundaries

The bars at the edges of a program that does not fit the screen were
sixteen pixels thick over fifteen-pixel rows, so a bar's top edge sat one
pixel into the row above it. The strip cleared beneath it and the area that
answered to a tap were both a row too high: the bar blanked the line of
text above it, and a tap on it could land on the cell beneath instead.

A bar is now a whole number of cells thick, so it sits exactly on a row or
column boundary, and the cells it covers are worked out from the rectangle
it actually occupies. Nothing else changes; the golden screens for the six
scrolling views were regenerated to match.
