# Programs

Twenty-one IRCIS programs for pIRCIS, all of them bundled in the firmware too. Copy them to `/pircis/programs/` on an SD
card, or paste one into the web editor, and they show up under **SAVE**.

Every one fits inside 34 x 11, so the whole program stays on screen at the
large font -- no scrolling, no `ZOOM` needed -- while you watch it run.

| Program | Size | What it does |
|---|---|---|
| `hello` | 3 x 18 | prints Hello World; the smallest useful program here |
| `countdown` | 6 x 15 | 10 down to 1, then GO |
| `count` | 6 x 20 | 1 to 20 |
| `odds` | 6 x 26 | the odd numbers |
| `evens` | 6 x 26 | the even numbers |
| `threes` | 6 x 26 | multiples of three |
| `times7` | 6 x 26 | the seven times table |
| `squares` | 6 x 26 | 1 to 10 squared |
| `cubes` | 6 x 31 | 1 to 8 cubed |
| `powers2` | 6 x 25 | 1 2 4 8 ... 512 |
| `halving` | 6 x 25 | 1024 halved down to 2 |
| `triangle` | 6 x 25 | running totals: 10 19 27 34 ... 55 |
| `fib` | 6 x 27 | the first ten Fibonacci numbers |
| `binary` | 10 x 25 | a number in binary, backwards (from upstream IRCIS) |
| `dice` | 10 x 28 | rolls a die, and spawns one runner per pip |
| `clock` | 11 x 32 | "The time is 4:02pm." -- an invented time, every run |
| `coin` | 7 x 25 | HEADS or TAILS |
| `lotto` | 6 x 21 | six random numbers from 1 to 49 |
| `race` | 11 x 32 | four runners, each with a random handicap, so the order changes every run |
| `spiral` | 11 x 31 | one runner winds inward over every cell |
| `snake` | 11 x 30 | the boustrophedon weave, back and forth |

The last four are worth watching rather than reading: set `SYS > RUN VIEW` to
`RUNNERS` and the speed to `SLOW` or `FAST`.

`dice` is the one to try first. It rolls, prints "You have rolled N", and puts
N runners into a ring -- so the number it printed is the number of coloured
markers going round. It keeps orbiting until you stop it.

`racetrack` is Arjun Nair's own example, carried over unchanged from
[IRCIS](https://github.com/batman-nair/IRCIS) apart from a view tag and one
blank column. Three runners go round the track and the output is the order
they finish in -- it uses the random opcodes, so the race is a different race
every time. It is the biggest program here at 19 x 70, which is what the WIDE
view is for.

Four of them -- race, racetrack, snake and spiral -- carry a view tag telling pIRCIS how
to show them: `~ns`, meaning nothing under the grid and run it slowly, because
they are worth watching rather than reading. The rest carry no tag, which is
the same as asking for the defaults.

A tag needs no room of its own. None of its characters is an IRCIS command, so
a runner that crosses one steps over it, and the three that have one keep it on
a blank cell inside the program. Add your own the same way; the tag alphabet is
in the main README.

## How they are built

Most of them are the same shape: a counter in a global, a body that runs east
along one row and back west along the next, and a `?` that drops into an exit
lane when the counter reaches zero. Reading a westward row means reading it
right to left, which is why those rows look like `^?.N&.-'.N@.1'<`.

Two things worth knowing if you write your own:

- Arithmetic takes the **top** of the stack as the left operand. Push 10 then
  3 and `'-.` gives -7, not 7. Division is the same way round.
- A single quote starts a number and a blank ends it: `'42.` pushes 42. An
  all-digit literal is decimal; anything else is base64, so `'fU.` is not 42.

Every program here was checked against the reference IRCIS
(batman-nair/IRCIS) as well as against this port; the seventeen that do not
use `r`/`R` produce byte-identical output on both.
