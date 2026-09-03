# Learn IRCIS

[IRCIS](https://github.com/batman-nair/IRCIS) is a programming language where
the program is a **grid of characters**, and a **runner** walks around it doing
what it steps on.

This guide starts from nothing and builds up. Every example is a real program.
You can type any of them into [pIRCIS](https://github.com/jamesleaver/pIRCIS)
and press play, or run them on a computer.
By the end you will be able to write your own.

> **Finding a program.** Programs are named here as the device names them, and
> the folder in brackets is the one they sit in under **PROG**. The link goes
> to the same file in this repository, which is laid out the same way:
> [Dice Roll](programs/Watching/Dice-Roll.txt) (Watching) is
> `programs/Watching/Dice-Roll.txt`.

> **You do not need the hardware.** The emulator runs the same thing in a
> window on your own computer: same interpreter, same screen, same programs.
> [How to set it up](README.md#run-it-on-your-computer-instead) is in the
> readme. In there, set `SYS > KEYBOARD` to `REAL` and you can type straight
> into the grid with your own keyboard, which makes working through this guide
> much quicker.

**Contents**

1. [The runner (the grid)](#1-the-runner-the-grid)
2. [Saying something (the stack)](#2-saying-something-the-stack)
3. [The print loop (loops)](#3-the-print-loop-loops)
4. [Numbers (integermode)](#4-numbers-integer-mode)
5. [Arithmetic, and the rule that is easy to get wrong (operators)](#5-arithmetic-and-the-rule-that-is-easy-to-get-wrong-operators)
6. [Remembering things (variables)](#6-remembering-things-variables)
7. [Asking questions (conditions)](#7-asking-questions-conditions)
8. [Throwing things away (clearing the stack)](#8-throwing-things-away-clearing-the-stack)
9. [Using a value twice (copying on the stack)](#9-using-a-value-twice-copying-on-the-stack)
10. [Counting loops (counters)](#10-counting-loops-counters)
11. [Chance (random numbers)](#11-chance-random-numbers)
12. [Making choices (branching)](#12-making-choices-branching)
13. [Waiting (pauses)](#13-waiting-pauses)
14. [More than one runner (splitting)](#14-more-than-one-runner-splitting)
15. [Letters out of numbers (base64)](#15-letters-out-of-numbers-base64)
16. [The rest of the arithmetic (powers and bit operations)](#16-the-rest-of-the-arithmetic-powers-and-bit-operations)
17. [Two jobs for one character (modes)](#17-two-jobs-for-one-character-modes)
18. [Quirks worth knowing (comments and hidden programs)](#18-quirks-worth-knowing-comments-and-hidden-programs)
19. [Reading a whole program (a worked example)](#19-reading-a-whole-program-a-worked-example)
20. [Build something (exercises)](#20-build-something-exercises)
21. [Cheat sheet](#cheat-sheet)

---

## 1. The runner (the grid)

A program is a rectangle of characters. The runner starts in the **top-left
corner**, facing **east**, and takes one step at a time.

Four characters steer it:

| | |
|---|---|
| `>` | go east |
| `v` | go south |
| `<` | go west |
| `^` | go north |

A full stop `.` is a **blank**, and so is a space. The two are interchangeable
everywhere: anything said in this guide about one is true of the other. The
runner walks straight over a blank and keeps going the way it was already
going. Blanks are how you leave space.

Almost every program here uses `.` rather than spaces, because on a screen full
of grid you can count dots along a row and you cannot count spaces.

`!` stops the runner.

Here is a runner that goes east, turns south, turns west, and stops:

```
>..v
....
!..<
```

Nothing is printed, but it is a real program.

There is only one `v`, not two. Once the runner is heading south it keeps going
south across the blanks on its own. **You only need an arrow where you want the
direction to change.**

Strictly the `>` in the corner is not doing anything either, because the runner
already starts facing east. I write it anyway, and so does every bundled
program, because it shows where things begin.

On the device, set `SYS > UNDER GRID` to `RUNNERS` and the speed to `SLOW`, and
you can watch it walk the path.

**A runner that leaves the grid dies.** Plenty of programs end that way on
purpose.

---

## 2. Saying something (the stack)

To print, you need two things: something on the **stack**, and a `#` to print
it.

The stack is a pile. You put things on top and you take them off the top. Last
in, first out, like a stack of plates.

There are only two things you can do to it, and they have names worth learning
now because everything later is built out of them.

- **Push** puts something on top.
- **Pop** takes the top one off. Whatever was underneath is on top now.

A double quote `"` turns **stack mode** on, and the next `"` turns it off.
Everything between them is pushed onto the stack, one character at a time.

`#` pops one thing and prints it. Printing and removing are the same action
here, so a value you print is gone afterwards.

```
>"iH"##!
```

prints `Hi`

Read that carefully, because it explains something unexpected. Here is what
each character does to the stack. The top of the pile is shown on the right.

| reads | does | stack afterwards |
|---|---|---|
| `"` | turn stack mode on | empty |
| `i` | push `i` | `i` |
| `H` | push `H` | `i H` |
| `"` | turn stack mode off | `i H` |
| `#` | pop `H` and print it | `i` |
| `#` | pop `i` and print it | empty |

`H` went on last, so it came off first.

**This is why text in IRCIS programs is written backwards.** The stack hands it
back to you in reverse, so you write it in reverse to start with.

Try it with a longer word:

```
>"olleh"#####!
```

prints `hello`

Five characters, five `#`. That is fine for five and unpleasant for fifty,
which is what the next section is for.

---

## 3. The print loop (loops)

Instead of one `#` per character, send the runner round in a circle so it hits
the same `#` over and over.

This is [Hello World](programs/Talking/Hello-World.txt) (Talking), the smallest
useful program on the device:

```
>"!dlroW olleH">#v
...............^.<
```

prints `Hello World!`

Follow the runner:

1. `>` sends it east.
2. `"` starts stack mode; everything up to the next `"` goes on the stack.
3. `>` east again, then `#` prints one character, then `v` sends it south.
4. On the bottom row `<` sends it west, then `^` sends it north.
5. It lands back on the `>` and does `#` again.

Round and round, one character per lap. When the stack is empty the `#` has
nothing to print, and the runner stops. **Running out of stack is how this
program ends**, and it is meant to.

The loop is only four cells: `>` `#` `v` on the top row, and `^` `<` below. The
`^` sits under the `>`, and the `<` sits under the `v`.

That `>` after the closing quote looks pointless, since the runner is already
heading east when it gets there. On the first pass it is. But look at where the
loop comes back: the `^` pushes the runner up onto that exact cell, travelling
north. Without a `>` to turn it east again it would carry on off the top of the
grid, and you would get `H` and nothing else.

**An arrow is only spare if the runner never arrives at that cell from some
other direction.** In a loop, it usually does.

> **Try it:** change the text to your own name. Remember to write it backwards,
> and keep the four loop cells lined up underneath.

### The string does not have to be a straight line

Stack mode pushes whatever the runner walks over, so the runner can turn while
it is reading, as long as the quote is closed first: `"` turns stack mode off,
the arrow turns the runner, and the next `"` turns stack mode back on. Each
straight run between quotes is a slice of the message, still written
backwards, and they arrive on the stack in the order the runner reads them.

[Two Dimensions](programs/Talking/Two-Dimensions.txt) (Talking) reads its
string boustrophedon -- one row east, the next row west:

```
>".eno naht re"v.
v"ons are bett"<.
>"isnemid owT">#v
..............^.<
```

prints `Two dimensions are better than one.`

Read the middle row from right to left, because that is the way the runner
walks it. Turning round costs two arrows: `v` at the end of a row and `<` under
it, then the quote reopens.

[Greeting](programs/Talking/Greeting.txt) (Talking) reads its string along a
square wave, three characters across and two down or up at a time:

```
>".di"v.....>" mo"v.....>"H">#v
......"....."....."....."...^.<
......r.....a.....r.....e......
......g..... .....f.....l......
......"....."....."....."......
......>" D2"^.....>" ol"^......
```

prints `Hello from a 2D grid.`

[Advice](programs/Talking/Advice.txt) (Talking) reads its string along a spiral,
and the print loop sits in the middle where the spiral runs out:

```
>".niaga no "v
.>"T">#v....."
."...^.<.....d
.u...........n
.r...........a
."..........."
.^"n it off "<
```

prints `Turn it off and on again.`

None of the three prints anything different from the plain version. The shape
is the point: the runner reads the program, and the program can be laid out
however you like.

---

## 4. Numbers (integer mode)

A single quote `'` starts a **number**, and a blank ends it.

```
>'42.#!
```

prints `42`

`'42.` pushes the number forty-two. The `.` on the end is the blank that says
the number stops there, rather than a decimal point.

Then `#` prints the number.

Between the `'` and that blank the runner is in **integer mode**, in the same
way that `"` puts it in stack mode. It is worth knowing the name, because
several characters mean one thing in integer mode and something else outside
it. [Section 17](#17-two-jobs-for-one-character-modes) sets them all out.

Numbers and characters are different things on the stack. `#` prints a character
as a character and a number as a number.

You can push several:

```
>'7.'8.'9.###!
```

prints `987`

Nine came off first, because it went on last.

Those three digits run together because nothing separates them. `$` starts a
new line. It takes nothing off the stack and prints nothing of its own, it just
moves the output on:

```
>'7.#$'8.#$'9.#!
```

prints:

```
7
8
9
```

Most of the examples here print one short thing and then stop, so they have no
call for it.

---

## 5. Arithmetic, and the rule that is easy to get wrong (operators)

Arithmetic happens in **integer mode**, which [section
4](#4-numbers-integer-mode) named: between a `'` and the blank that ends it. So
`'+.` means go into integer mode, add, come out again. It takes the top two
things off the stack and puts the answer back.

```
>'2.'3.'+.#!
```

prints `5`

Here is the rule that is easiest to get wrong. I have made this mistake more
than once:

> **The operation is `top` first, then the one underneath.**
> Push 10, then push 3, and `'-.` gives you **3 − 10 = −7**. Not 7.

```
>'10.'3.'-.#!
```

prints `-7`

| reads | does | stack afterwards |
|---|---|---|
| `'10.` | push 10 | `10` |
| `'3.` | push 3 | `10 3` |
| `'-.` | pop 3, pop 10, subtract 10 from 3, push −7 | `-7` |
| `#` | pop −7 and print it | empty |

Addition and multiplication do not care, because 3+10 and 10+3 are the same. But
subtraction, division and remainder do. If you want 10 − 3, push them the other
way round:

```
>'3.'10.'-.#!
```

prints `7`

The operators are `+` `-` `*` `/` and `%`. Division throws away the fraction:

```
>'2.'7.'/.#!
```

prints `3`

`%` is the remainder, which is the part division threw away:

```
>'2.'7.'%.#!
```

prints `1`

Seven divided by two is three, with one left over. `/` gives you the three and
`%` gives you the one. Both follow the same top-first rule as subtraction.

Remainder is how you test whether a number divides evenly. Push 2, push the
number, and `'%.` leaves 0 if it is even and 1 if it is odd. That is exactly
the sort of answer `?` can act on.

Those five are not the whole set. There are six more, including powers and a
group that works on the bits of a number rather than its value. They are in
[section 15](#15-letters-out-of-numbers-base64), once numbers have been properly
covered.

---

## 6. Remembering things (variables)

The stack is fine for one or two values. Keeping track of more than that on the
stack is hard, and variables are easier.

- `&N` **saves** the top of the stack into a variable called `N`.
- `@N` **pushes** what is in `N` back onto the stack.

As with a number, a blank ends the name.

```
>'8.&N.@N.@N.'*.#!
```

prints `64`

That is eight squared. Step by step, with the stack after each piece. The top of
the stack is on the right.

| reads | does | stack afterwards |
|---|---|---|
| `'8.` | push 8 | `8` |
| `&N.` | save 8 into `N`, leaving it where it is | `8` |
| `@N.` | push what is in `N` | `8 8` |
| `@N.` | push it again | `8 8 8` |
| `'*.` | multiply: pop the top two, push 64 | `8 64` |
| `#` | pop 64 and print it | `8` |
| `!` | stop | `8` |

Look at the 8 still sitting there at the end. That is `&N.` doing exactly what
it says: it copied the value into `N` and left the original alone. Nothing here
needs it again and a leftover does no harm, but in a loop it would pile up one
per lap. [Section 8](#8-throwing-things-away-clearing-the-stack) is about
clearing those away.

This example does not really need a variable. The stack could hold the 8 by
itself. Variables are useful when you need a value again later, after other
things have been pushed on top of it.

Almost every program here uses a single letter, because every character costs a
cell and grids are small. Longer names work if you want them, so long as they
are all letters. `&size.` is fine; `&size2.` is not, and the runner stops dead
when it reaches it. Nothing on the grid says why, but `SYS > DIAGNOSTICS` will
tell you: *runner 0 died at row 0, col 7: Variable name 'size2' should contain
only alphabets*. It gives the cell as well as the reason, so you know where to
look. That panel is the first place to go whenever a program stops for no
visible reason.

**Capital letters mean something.** A variable with a lowercase name belongs to
one runner. A variable with a CAPITAL name is **global**, and every runner
shares it. That matters once you have more than one runner, in [section
14](#14-more-than-one-runner-splitting). The counting programs all use `&N` for
exactly this reason.

---

## 7. Asking questions (conditions)

`?` looks at the top of the stack and asks: is it zero?

- **Not zero** — carry straight on.
- **Zero** — turn. It looks left first, and goes that way if there is something
  there; otherwise it turns right.

"Left" and "right" are from the runner's point of view. Heading east, left is
north and right is south.

`?` **only looks at the top of the stack and leaves it there.** That is easy to
forget. Whatever `?` looked at is still on the stack afterwards, so something
further on usually has to remove it.

```
>'0.?."on"##!
....v
....>"sey"###!
```

prints `yes`

The `?` saw zero, so it turned. North was blank, so it went south, found the
`>`, and printed `yes`. Change `'0.` to `'1.` and it carries straight on and
prints `no` instead.

---

## 8. Throwing things away (clearing the stack)

Values pile up on the stack. Every `?` leaves behind the thing it looked at.
Every `&N.` leaves behind the value it copied. Any sum whose answer you never
print stays there too.

In a short program that does no harm. In a loop it does, because whatever one
lap leaves behind is still sitting there on the next one, and the pile grows
every time round.

`&` followed by a number removes that many values from the top of the stack.

- `&1.` removes one.
- `&2.` removes two.

This is the pop from [section 2](#2-saying-something-the-stack), with nothing
printed.

```
>'1.'2.'3.&2.#!
```

prints `1`

| reads | does | stack afterwards |
|---|---|---|
| `'1.` | push 1 | `1` |
| `'2.` | push 2 | `1 2` |
| `'3.` | push 3 | `1 2 3` |
| `&2.` | remove the top two | `1` |
| `#` | pop 1 and print it | empty |

`&N.&1.` is the pair you will see all through the bundled programs. The `&N.`
saves the value, and the `&1.` removes the copy that the save left behind.

---

## 9. Using a value twice (copying on the stack)

`@` followed by a number does the opposite of `&` followed by a number. Where
`&` throws values away, `@` copies one back to the top. `@0.` copies the top,
`@1.` copies the one below it, and so on. The original stays where it was.

```
>'7.'8.'9.@1.#!
```

prints `8`

| reads | does | stack afterwards |
|---|---|---|
| `'7.` | push 7 | `7` |
| `'8.` | push 8 | `7 8` |
| `'9.` | push 9 | `7 8 9` |
| `@1.` | copy the item one below the top | `7 8 9 8` |
| `#` | pop 8 and print it | `7 8 9` |

`@0.` is the one worth remembering, because it duplicates the top. That is how
you use a value twice when the first use is going to remove it:

```
>'8.@0.'*.#!
```

prints `64`

Eight squared, the same answer as the variable in [section
6](#6-remembering-things-variables), without needing a variable. Push 8, copy
it, and multiply the two copies together.

**`&` and `@` each do two jobs, and what comes after them decides which.**
Letters mean a variable: `&total.` and `@total.` save and fetch something
called `total`. Digits mean the stack itself: `&1.` removes one item and `@1.`
copies one back. That is worth being careful about, because a name like `n`
looks as though it ought to mean a number and does not. It is a variable
called `n`.

---

## 10. Counting loops (counters)

Now put `?` together with a variable and you have a loop.

This is [Countdown](programs/Counting/Countdown.txt) (Counting):

```
>'10.&N.v......
v.......<......
>&1.@N.#" "#..v
^?.N&.-'.N@.1'<
.>"OG"##!......
```

prints `10 9 8 7 6 5 4 3 2 1 GO`

It looks alarming. It is four simple pieces:

1. **Row 0** sets `N` to 10, then drops down.
2. **Row 1** carries the runner back to the left edge.
3. **Row 2** is the body: `&1.` cleans up, `@N.` pushes the counter, `#` prints
   it, `" "#` prints a space.
4. **Row 3 counts down.** The runner travels *west* along it, so it reads right
   to left. It works out `N − 1` and saves it, and `?` decides whether to go
   round again or drop to row 4, which prints `GO`.

Here is the first lap, in the order the runner meets things.

| row | reads | does | stack afterwards |
|---|---|---|---|
| 0 | `'10.` | push 10 | `10` |
| 0 | `&N.` | save 10 into `N`, leaving it where it is | `10` |
| 2 | `&1.` | remove it | empty |
| 2 | `@N.` | push what is in `N` | `10` |
| 2 | `#` | pop 10 and print it | empty |
| 2 | `" "#` | push a space, then pop and print it | empty |
| 3 | `'1.` | push 1 | `1` |
| 3 | `@N.` | push what is in `N` | `1 10` |
| 3 | `'-.` | pop 10, pop 1, take 1 from 10, push 9 | `9` |
| 3 | `&N.` | save 9 into `N`, leaving it where it is | `9` |
| 3 | `?` | look at 9: not zero, so carry on west | `9` |

The `^` at the end of row 3 sends the runner back up into row 2, and the lap
starts again. The 9 that `?` left behind is still there, which is what the
`&1.` at the start of row 2 is for. Without it the stack would grow by one
value every lap.

**A row the runner travels west along is written back to front.** There is
nothing special about `^?.N&.-'.N@.1'<`. It is ordinary instructions in the
order the runner meets them, which happens to be right to left.

> **Try it:** change `'10.` to `'5.` and it counts down from five. Change
> `"OG"##` to something else. Remember that text goes onto the stack in
> reverse, which is a separate thing from the row running west.

---

## 11. Chance (random numbers)

Two characters give you a random number.

- `r` pushes a random 0 or 1.
- `R` takes a limit off the stack and pushes a random number from 0 up to it.

```
>'5.R#!
```

prints a number from 0 to 5

`r` is the one to reach for when a program has to choose between two things,
and `R` when it has to choose between many. Both leave an ordinary number on
the stack, so everything already covered works on it: `?` can test it,
arithmetic can change it, and a variable can hold it.

---

## 12. Making choices (branching)

A **branch** is a point where the runner can go two ways. The two routes out of
it are its **arms**. [True Or False](programs/Deciding/True-Or-False.txt)
(Deciding) is a branch whose two arms **meet up again**:

```
>r?"eurt"####....v......
..>"eslaf"#####v........
...............>.>"."#!.
```

prints `true.` or `false.`

`r` pushes a random 0 or 1. `?` carries on east for 1 and turns south for 0.
Each arm prints a **counted** number of characters, four `#` for `true` and
five for `false`, rather than looping until the stack runs dry.

That is the important bit. A print loop ends when the stack runs out, and the
runner dies with it. Here each arm still has to reach the shared ending on
row 2, so it has to be alive when it gets there. Counting the `#` keeps it
alive.

The true arm drops down column 17. On the way it passes through row 1, which
belongs to the false arm, but column 17 of that row is blank so nothing
happens. **A runner does whatever it passes over, including on a row that is
meant for some other part of the program.** Had the true arm dropped through
one of the false arm's `#` characters instead, it would have printed an extra
character on the way past. That is a hard mistake to find, because the grid
still looks right.

---

## 13. Waiting (pauses)

`p` takes a number off the stack and makes that runner wait that many steps
before carrying on.

```
>'20.p"iH"##!
```

prints `Hi`

The output is no different. The difference is on screen: at a slow speed you
can watch the runner sit on the `p` for twenty steps and then move off. A pause
inside a loop slows the program to a pace you can follow, which is how the
programs that draw things stay watchable.

A step here means one turn of the whole program, not one step of this runner.
(Arjun Nair's README calls these ticks. They are the same thing.) So a runner
that pauses for twenty steps stands still while everything else takes twenty.
That is what makes `p` useful once a program has several runners in it, which is
the next section: [Racetrack](programs/Watching/Racetrack.txt) (Watching)
gives each lane a different pause, and that is the only reason the lanes finish
at different times.

`p` needs a **number**, and a number is not the same thing as a character that
looks like one. [Section 4](#4-numbers-integer-mode) mentioned this in passing:
`'3.` pushes the number three, and `"3"` pushes the character `3`. Written down
they look identical. On the stack they are two different things, and `p` will
only take the first.

```
>"3"p"iH"##!
```

prints nothing

The runner gets to the `p`, finds a character where it needed a number, and
stops there, so the `"iH"##` after it never runs at all. `SYS > DIAGNOSTICS`
gives the reason: *Pause time must be an integer greater than or equal to 0*.
Changing `"3"` to `'3.` is the whole fix.

---

## 14. More than one runner (splitting)

`*` splits the runner. It looks at the four neighbouring cells, and for each one
holding **that direction's own arrow**, it sends a runner that way.

So a `*` with a `>` to its east and a `v` below it becomes two runners.

```
>.*>"A"#!
..v......
..>"B"#!.
```

prints `AB`

The neighbour has to be the *matching* arrow. A `*` next to another `*` does not
split towards it, which is a real trap when you are lining several up in a row.

This is what [Dice Roll](programs/Watching/Dice-Roll.txt) (Watching) uses:
it prints the number it rolled, then splits that many runners into a ring, so
you can count the answer going round the screen. [Section
19](#19-reading-a-whole-program-a-worked-example) goes through the whole of it.

[Section 6](#6-remembering-things-variables) said a lowercase name belongs to
one runner and a CAPITAL name is shared. With one runner that makes no
difference. Here it does.

When `*` splits a runner, the new one starts with a **copy** of the lowercase
variables belonging to the runner it came from. The two copies are separate
from then on: if one runner saves a new value into `k`, the other still sees
the old one. A CAPITAL `N` is
a single value that both runners read and write, so a change made by one is
seen by the other.

```
>'5.&N.&1.*>'1.@N.'-.&N.#!...
..........v..................
..........>'9.p'1.@N.'-.&N.#!
```

prints `43`

Both runners take 1 off the counter and print the result. The `'9.p` holds the
second runner back until the first has finished. The first prints 4. The second
then reads the 4 that the first one saved, takes 1 off, and prints 3.

Change every `N` in that program to a lowercase letter, `k` say, and it prints
`44` instead, because each runner then has its own counter and both start from
the 5 they inherited.

The wait matters. Without it the two runners move a step at a time in turn, so
both read the counter before either saves, and a shared `N` gives the same `44`
as a private `k`. Sharing only shows when one runner gets there after the
other.

---

## 15. Letters out of numbers (base64)

`%` prints a number as **base64 characters**.

Base64 counts the way ordinary numbers do, except there are sixty-four digits
instead of ten. The digits are the capitals, then the lowercase letters, then
the numerals, then two punctuation marks:

| digit | value in base 10 |
|---|---|
| `A` to `Z` | 0 to 25 |
| `a` to `z` | 26 to 51 |
| `0` to `9` | 52 to 61 |
| `+` | 62 |
| `/` | 63 |

So **`A` is zero**, `B` is 1, `Z` is 25, `a` is 26, `z` is 51, the character
`0` is 52, and `/` is 63.

Each place is worth 64 times the place to its right, in the same way each place
in an ordinary number is worth 10 times the one to its right. `BA` is
1 × 64 + 0, which is 64. `BB` is 65.

That is enough to hide a whole word inside one number:

```
>'118489298.%!
```

prints `HEADS`

Worked out by hand: `H` is 7, `E` is 4, `A` is 0, `D` is 3, `S` is 18.

    7 x 64x64x64x64  = 117440512
    4 x 64x64x64     =   1048576
    0 x 64x64        =         0
    3 x 64           =       192
    18               =        18
                       ---------
                       118489298

One number, five letters.

You do not have to work that out by hand, though. **A number with letters in it
is read as base64 when you write it**, so you can type the word straight in:

```
>'HEADS.%!
```

prints `HEADS`

`'HEADS.` pushes 118489298, the same number as before. Print it with `#`
instead of `%` and `118489298` is what comes back.

The rule is: a number written with digits only is an ordinary base 10 number,
and if a letter, `+` or `/` appears anywhere in it, the whole number is base64.

That second half is easy to miss. `'0A.` is not a zero followed by an `A`. The
`A` puts the whole number into base64, where the character `0` means 52, so
`'0A.` is 52 x 64, which is 3328.

That is how [Coin Flip](programs/Deciding/Coin-Flip.txt) (Deciding) works. It
has no branch in it at all:

```
>'200311296.r'*.'118489298.'+.%!
```

prints `HEADS` or `TAILS`

`HEADS` is 118489298 and `TAILS` is 318800594. The distance between them is
200311296, and `r` gives either 0 or 1, so multiplying that distance by `r`
either adds nothing or adds the whole way:

| reads | does | if `r` gave 0 | if `r` gave 1 |
|---|---|---|---|
| `'200311296.` | push the distance between the two words | `200311296` | `200311296` |
| `r` | push 0 or 1 | `200311296 0` | `200311296 1` |
| `'*.` | multiply the top two | `0` | `200311296` |
| `'118489298.` | push `HEADS` | `0 118489298` | `200311296 118489298` |
| `'+.` | add the top two | `118489298` | `318800594` |
| `%` | print it as base64 | `HEADS` | `TAILS` |

Arithmetic did the choosing, and `%` turned the answer back into a word.

It is also how the sneakiest programs on the device work. [Insult
Machine](programs/Decoding/Insult-Machine.txt) (Decoding) contains no
letters at all. Every word is a number, so you cannot read the punchline off the
grid. Have a look at it before you run it.

**Doing it the long way.** [Base 64](programs/Decoding/Base-64.txt)
(Decoding) works the same sum out by hand, so you can watch the place values
pile up:

```
>'7.'64.'*.'4.'+.'64.'*.'0.'+.'64.'*.'3.'+.'64.'*.'18.'+.&N.%" = "###@N.#$v
v.........................................................................<
>'HEADS.&N.%" = "###@N.#!
```

prints:

```
HEADS = 118489298
HEADS = 118489298
```

Row 0 never mentions base64. It pushes 7, multiplies by 64, adds 4, multiplies
by 64, adds 0, and carries on through the five digit values. That is the same
sum set out above. Row 2 reaches the same number by writing `'HEADS.` and
letting the language do the work. Both rows then print their number twice,
once with `%` and once with `#`. The two lines come out identical because the
two routes arrive at the same number.

**How big a number can get.** Numbers are 32-bit signed integers, so they run
from −2147483648 to 2147483647. One past the top and the number wraps round to
the bottom:

```
>'2147483647.'1.'+.#!
```

prints `-2147483648`

That ceiling applies to base64 as well. Six base64 digits could reach far
higher than an integer will hold, so the first digit has only two bits left to
use: `A` and `B` are the only leading digits that fit in six. `B/////` is
2147483647, the largest number there is, and `C/////` runs past the end and
comes back negative.

> **Careful:** an `A` at the start of a number is a leading zero, and leading
> zeros are not kept. `HEAD`, `AHEAD` and `AAHEAD` are all 1851395, and `%`
> prints `HEAD` for all three. An `A` anywhere else is safe, so `HEADS` comes
> back whole.

---

## 16. The rest of the arithmetic (powers and bit operations)

Six more operators work exactly like the five in [section
5](#5-arithmetic-and-the-rule-that-is-easy-to-get-wrong-operators). All of them
are written in integer mode, and all take the top two values off the stack, top
first.

**`^` raises to a power.**

```
>'2.'10.'^.#!
```

prints `100`

Ten squared. The top of the stack is the number, and the one underneath is the
power to raise it to.

The other five work on a number's **bits**. Every whole number is stored as a
row of bits, which is what writing it in base 2 means: 10 is `1010` and 12 is
`1100`. These operators line the two rows up and compare them a bit at a time.

`&` keeps a bit only where **both** numbers have one:

```
>'12.'10.'&.#!
```

prints `8`

`1010` and `1100` have a bit in common only in the eights column, so 8 is what
is left.

`|` keeps a bit where **either** has one:

```
>'12.'10.'|.#!
```

prints `14`

`V` is exclusive or: a bit where **exactly one** of the two has one, so the
places where they agree cancel out:

```
>'12.'10.'V.#!
```

prints `6`

`<` and `>` slide the bits along. Left by one doubles the number, right by one
halves it and drops anything that falls off the end:

```
>'1.'8.'<.#!
```

prints `16`

```
>'1.'8.'>.#!
```

prints `4`

The top is the number being moved, and the one underneath is how far.

**These characters are doing two jobs.** `^`, `<` and `>` are also the arrows,
`&` is also how you save a variable and how you drop values, and `V` is an
ordinary letter. They only mean arithmetic between a `'` and the blank that
ends the number. Anywhere else `<` is still west.

There is a further catch, and it follows from [section
15](#15-letters-out-of-numbers-base64). `+`, `/` and `V` are base64 digits as
well as operators. If the character straight after one of them could also be
part of a number, the whole thing is read as a number instead:

```
>'VA.#!
```

prints `1344`

That is the base64 number `VA`, not an exclusive or. `'V.` is the operator,
because a blank cannot be part of a number.

---

## 17. Two jobs for one character (modes)

The runner is always in one of three modes, and a character means different
things in each.

- **Normal.** The runner is walking. A character IRCIS knows is carried out as
  an instruction, and every other character is walked over and ignored.
- **Stack mode**, between a pair of `"`. Every character is pushed. Nothing is
  an instruction, not even an arrow.
- **Integer mode**, between a `'` and the blank that ends it. The characters
  are read as one number, or as an operator.

Almost every character that looks like it does two jobs is really one job in
normal mode and another in integer mode:

| character | in normal mode | in integer mode |
|---|---|---|
| `*` | split into more runners | multiply |
| `%` | print the top as base64 | remainder |
| `&` | save a variable, or drop values | bitwise and |
| `^` | go north | raise to a power |
| `<` | go west | shift the bits left |
| `>` | go east | shift the bits right |
| `+` `-` `/` | nothing, walked over | add, subtract, divide |
| `V` `\|` | nothing, walked over | exclusive or, or |

`*` is the clearest case. On its own it splits the runner:

```
>.*>"A"#!
..v......
..>"B"#!.
```

prints `AB`

Put the same character in integer mode and it multiplies:

```
>'6.'7.'*.#!
```

prints `42`

Same character, two entirely different jobs, and the only thing that decides it
is whether a `'` opened a number first.

`%` is the other one worth watching, because both of its jobs turn up in the
same programs. In normal mode it prints the top of the stack as base64. In
integer mode it is the remainder:

```
>'2.'7.'%.%!
```

prints `B`

The first `%` is in integer mode, so it takes the remainder of 7 and 2, which
is 1. The second is in normal mode, so it prints that 1 as base64, and 1 in
base64 is `B`.

---

## 18. Quirks worth knowing (comments and hidden programs)

**In normal mode, most characters do nothing.** The runner only acts on
these:

```
< > ^ v   + - * / %   & | V   ? ! $ #   " '   @   p r R
```

Everything else is walked straight over and ignored: every other letter, every
digit outside a number, commas, brackets, all of it.

That holds while the runner is between instructions, which is most of the time.
It does not hold while the runner is part-way through reading something:

- Between a pair of `"` quotes **every** character is pushed, spaces and commas
  included. That is what stack mode is for.
- After `'`, characters are read as part of the number until a blank ends it. A
  character that cannot be part of a number stops the runner: `'1,2.` dies with
  *Non integer character in integer processing*.
- After `&` or `@`, characters are read as a variable name until a blank ends
  it. Anything that is not a letter stops the runner: `&N,.` dies with
  *Variable name 'N,' should contain only alphabets*.

A blank ends a number and it ends a variable name, so once you have written
`'42.` or `&N.` the runner is between instructions again and anything may
follow.

That means **you can write notes to yourself, and to whoever runs your program,
right inside the grid**:

```
>"iH"## this text is walked over and ignored !
```

prints `Hi`

The runner steps on `t`, `h`, `i`, `s` and so on, shrugs, and carries on.

Two things to watch:

- **`p`, `r`, `R` and `V` are letters that *do* something.** `p` pauses, `r` and
  `R` push random numbers, `V` is exclusive-or. A note containing the word
  "program" has a `p` and an `r` in it, and both will fire.
- The safe way is to put your text **where no runner ever goes**. Cells nothing
  steps on are never read at all, so anything goes there: any letter, any
  punctuation.

[Morse Decoder](programs/Decoding/Morse-Decoder.txt) (Decoding) uses
exactly that. Its second, third and fourth rows are plain English instructions,
and the runner never reads them: it comes down the right-hand end of the first
row and falls straight past them.

[Racetrack](programs/Watching/Racetrack.txt) (Watching) does the same with
its title and its footer.

Taken to its limit, the whole program can be hidden inside something that reads
as ordinary writing. [Nothing To See](programs/Watching/Nothing-To-See.txt)
(Watching) is a paragraph of plain English:

```
It looks like nothing much, even
when you read it twice. There's nothing here.
Every word of it is ordinary if you check.
It is a note about nothing, arranged plainly.
These lines are the sort you could skim past.
Nobody would give it a quick inspection.
It says nothing much, and does nothing.
There is no trick here at all. Nothing.
Not a word of it, not even 15% of it.
There is nothing here, at all! Nothing.
```

prints `ircis`

Nothing is out of place and there is no tag on it. The runner sets off east
along the first line, walks over `It looks like nothing much, e` because none
of those characters is an instruction, and reaches the `v` in **even**. That
sends it south, and from there it only ever reads column 29. Down that one
column the lines read `'`, `i`, `r`, `c`, `i`, `s`, `.`, `%`, `!`. Those are
the apostrophe in **There's**, letters from the middle of five ordinary words, a
full stop, the percent sign in **15%**, and the exclamation mark at the end.
That spells `'ircis.%!`: push the base64 number `ircis`, print it, stop.

Writing one is a fitting exercise. Pick a column, decide what the program has
to say down it, and then write sentences that happen to carry the right
character at that position.

**And because the path is the program, the path can be a picture.**
[Motto](programs/Watching/Motto.txt) (Watching) spells out **IRCIS** on the screen
by splitting a runner off at each letter to walk its shape. The letters only
exist as the routes the runners take, and nothing in the grid draws them. Turn on the
`~t` view tag and the path stays on screen instead of fading.

**View tags.** These are a pIRCIS addition, not part of IRCIS. Arjun Nair's
interpreter knows nothing about them, and a program carrying one still runs
there, because the tag is only a run of ordinary characters that the runner
walks over. On this device a program can use one to ask to be shown a
particular way.

**A tag is a tilde `~` followed by letters**, written on any blank cell. The
tilde is what marks it as a tag, and the letters after it come from this list,
in any order:

```
under the grid:  n  nothing    d  runner readout
speed:           s  slow   m  med   q  quick   f  full
path:            t  keep the runners' path on screen
hold the view:   h  do not follow the runners while it runs
start position:  <row>,<col> and one of N E S W
```

So `~nst` means: nothing underneath, slow, keep the path. `h` is worth knowing
for a program that draws: without it the view follows the runner and the
picture moves out from under you. None of the tag
characters is an instruction, so a runner that crosses one steps over it.

---

## 19. Reading a whole program (a worked example)

Everything so far has been shown a few cells at a time. This section takes one
of the bundled programs and goes through all of it, which is the last thing
worth practising: the programs on the device are not longer than this one, only
wider.

[Dice Roll](programs/Watching/Dice-Roll.txt) (Watching) rolls a die, says
what it rolled, and then puts that many runners into a ring going round the
screen, so the number it printed is the number of markers you can count.

```
>'5.R'1.'+.&D.&K.v..........
v................<....<.....
>&2.@K.?*>'1.@K.'-.&K.^.....
.......vv...................
........>....v..............
........^....<..............
v......<....................
>&1.@D." dellor evah uoY">#v
.........................^.<
```

prints `You have rolled 4`, or whatever it rolled

**Row 0 makes the roll and remembers it twice.**

| reads | does | stack afterwards |
|---|---|---|
| `'5.` | push 5, the largest number wanted | `5` |
| `R` | take that 5 off and push a random number from 0 to 5 | `3` |
| `'1.` | push 1 | `3 1` |
| `'+.` | add, so the range becomes 1 to 6 | `4` |
| `&D.` | save the roll into `D`, leaving it where it is | `4` |
| `&K.` | save the roll into `K` as well | `4` |
| `v` | go south | `4` |

Two copies because they are used for two different jobs. `D` is the number to
print at the end. `K` is a counter that is about to be counted down to nothing,
and counting it down would destroy the answer if there were only one copy.

**Row 1 carries the runner back to the left edge**, the same as in
[Countdown](programs/Counting/Countdown.txt) (Counting).

**Row 2 is a loop that makes one new runner per lap.**

`&2.` clears the stack. On the first lap there is only one value on it, and
asking to remove two when there is one is not an error. It empties the stack
and carries on. On later laps there are two things to clear, which is why it
says two.

`@K.` pushes the counter and `?` looks at it.

- **Not zero.** The runner carries straight on east onto the `*`. Below the `*`
  is a `v` and to its east is a `>`, so it splits: one runner carries on east,
  and a new one heads south into the ring. The rest of the row works out
  `K − 1`, saves it, and the `^` sends the runner back up to row 1 and round
  again.
- **Zero.** Every runner has been made. `?` turns, and since north is blank it
  goes south instead, down to row 6 and along to row 7.

**Rows 3 to 5 are the ring.** A new runner drops in at the `>` on row 4 and
then goes round four cells forever: east along row 4 to the `v`, south to
row 5, west to the `^`, north back to where it started. Nothing is printed. The
runners are the output, and you count them.

**Row 7 prints the answer.**

`&1.` removes the zero that `?` left behind. `@D.` pushes the roll, which has
been sitting safely in `D` the whole time. Then `" dellor evah uoY"` pushes the
message backwards, so that it comes off the right way round.

The last four cells are the same print loop as [Hello
World](programs/Talking/Hello-World.txt) (Talking): `>` and `#` on row 7, `^`
and `<` on row 8. It goes round printing one character per lap: `Y`, `o`, `u`,
and so on, and when the message runs out the roll is the last thing left, so
`You have rolled ` is followed by the number. Then the stack is empty, the `#`
has nothing to print, and that runner stops. The ones in the ring keep going.

**What to take from it.** Every piece of that program is something from an
earlier section. A random number, an arithmetic step, two variables, a counting
loop, a `?` deciding when to stop, a split, and a print loop. The bundled
programs that look hardest, such as [Morse
Decoder](programs/Decoding/Morse-Decoder.txt) (Decoding),
[Pi](programs/Counting/Pi.txt) (Counting) and
[Racetrack](programs/Watching/Racetrack.txt) (Watching) are built the same
way out of the same parts. When one of them
looks impossible, find the loop first, then find what ends it, and read the rest
afterwards.

---

## 20. Build something (exercises)

You now know everything the bundled programs use. Here are three projects, in
order of difficulty.

**A times table.** Print the seven times table. Start from
[Countdown](programs/Counting/Countdown.txt) (Counting) and change the body:
push the counter, push 7, multiply, print. Look at [Seven
Times](programs/Counting/Seven-Times.txt) (Counting) if you get stuck, but try
it first.

**A yes/no machine.** Ask a question in the grid as a comment, then use `r` and
`?` to print one of two answers. [True Or
False](programs/Deciding/True-Or-False.txt) (Deciding) is the model. Then make
it four answers instead of two, by testing twice. [Magic Eight
Ball](programs/Deciding/Magic-Eight-Ball.txt) (Deciding) shows one way.

**A picture.** Send a runner on a path that draws a shape, with `~nst` so the
path stays on screen. [Spiral](programs/Watching/Spiral.txt) (Watching) and
[Bounce](programs/Watching/Bounce.txt) (Watching) are the simple ones, and
[Motto](programs/Watching/Motto.txt) (Watching) is a much harder one.

The `?` button in the editor's status bar opens the command list, so you do not
have to come back here for it.

When something does not work, the two questions that solve most problems are:

1. **Is the runner where I think it is?** Set `UNDER GRID` to `RUNNERS`, put it
   on `SLOW`, and watch. Turn on `SYS > STEP BUTTONS` and walk it one step at a
   time.
2. **Is the stack what I think it is?** Remember that `?` and `&name` both leave
   the value where it was.

---

## Cheat sheet

The commands and their names follow
[Arjun Nair's own table](https://github.com/batman-nair/IRCIS), so that this
guide and his README agree. Each row shows what you actually write.

**Anywhere in the grid**

| What you write | What it does |
|:---:|---|
| `>` `<` `^` `v` | move east, west, north, south |
| `.` or a space | blank, walked over |
| `!` | stop this runner |
| `"..."` | **stack mode**: every character between the quotes is pushed |
| `'...` | **integer mode**: a number or an operator, ended by a blank |
| `#` | pop the top and print it |
| `%` | pop the top and print it as base64 characters |
| `$` | print a new line |
| `?` | look at the top without removing it. Zero turns, anything else carries on |
| `*` | split into more runners |
| `r` | push a random 0 or 1 |
| `R` | push a random number from 0 up to the limit on top of the stack |
| `p` | pause this runner for the number of steps on top of the stack |
| `@name.` | push the value of the variable called `name` |
| `&name.` | copy the top of the stack into the variable `name`, leaving it there |
| `@2.` | copy the item 2 places below the top onto the top. `@0.` copies the top itself |
| `&2.` | remove 2 items from the top of the stack |

`@` and `&` both read whatever follows them, and what you write decides which
job you get. **Letters** mean a variable, and the name may be as long as you
like. **Digits** mean the stack itself: a position for `@`, and a count
for `&`.

**In integer mode only**

Each of these is three characters: the `'` that opens integer mode, the
operator, and the blank that closes it. Several of them appear in the table
above as well, doing something completely different outside integer mode.
[Section 17](#17-two-jobs-for-one-character-modes) sets out which.

All of them take the top two values off the stack, **top first, then the one
underneath**.

| What you write | What it does |
|:---:|---|
| `'+.` | add |
| `'-.` | subtract |
| `'*.` | multiply |
| `'/.` | divide, throwing away the fraction |
| `'%.` | remainder |
| `'^.` | raise to a power |
| `'&.` | bitwise and |
| `'\|.` | bitwise or |
| `'V.` | exclusive or |
| `'<.` | shift the bits left |
| `'>.` | shift the bits right |

**Six mistakes that are easy to make**

1. Text comes back off the stack backwards, so write it backwards.
2. Arithmetic is top-first: push 10 then 3 and `'-.` is −7.
3. `?` and `&N.` look at the top without removing it.
4. A cell holds one instruction. If a path crosses one of its own turns, it will
   take that turn again.
5. One letter makes a whole number base64, so `'0A.` is 3328, not zero.
6. Numbers and arithmetic need a `'` before them and a blank after them. A bare
   `*` splits the runner instead of multiplying.

---

The full command list and the language's own rules are in
[Arjun Nair's IRCIS](https://github.com/batman-nair/IRCIS), which is where all
of this comes from. Every program mentioned here is in
[`programs/`](programs/), with a table of what each one does.
