/* plinky-life - the flash a triggered cell makes.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   A voice's chosen cell used to light steadily until the voice moved on, which
   read as part of the playhead column rather than as a hit. A short burst marks
   the moment instead: the cell flares in the voice's colour and falls away, and
   a ring flickers over its neighbours on the way out.

   Levels are 0..256 so a colour can be scaled by a shift rather than a divide.

   Ages are unsigned differences of time_us(), which wraps every ~71 minutes.
   Unsigned subtraction gives the right answer across that wrap, so nothing here
   needs to know it happened. */

#define LIFE_SPARK_LEVEL_MAX 256

static inline int life_spark_alive(unsigned int age_us, unsigned int len_us) {
    return len_us != 0 && age_us < len_us;
}

/* The cell itself: full brightness at the strike, linear to nothing. */
static inline int life_spark_centre(unsigned int age_us, unsigned int len_us) {
    if (!life_spark_alive(age_us, len_us)) return 0;
    return (int)(LIFE_SPARK_LEVEL_MAX - (age_us * LIFE_SPARK_LEVEL_MAX) / len_us);
}

/* The ring of neighbours: nothing at the strike, a peak a third of the way
   through, then gone. Starting at zero is what makes it read as spreading
   outward rather than as the cell simply being fat for a moment. */
static inline int life_spark_ring(unsigned int age_us, unsigned int len_us) {
    if (!life_spark_alive(age_us, len_us)) return 0;

    unsigned int peak = len_us / 3;
    if (peak == 0) return 0;

    int level;
    if (age_us <= peak)
        level = (int)((age_us * LIFE_SPARK_LEVEL_MAX) / peak);
    else
        level = (int)(LIFE_SPARK_LEVEL_MAX -
                      ((age_us - peak) * LIFE_SPARK_LEVEL_MAX) / (len_us - peak));

    /* The ring is a suggestion, not a second cell. Half weight keeps a dense
       world from turning into a field of overlapping blobs. */
    level = level / 2;
    return level < 0 ? 0 : level;
}

/* A setting of 0..100 maps to how long a flash lasts. 0 is off; the rest runs
   from brisk to loose, and never so long that flashes from consecutive steps
   pile up at ordinary tempos. */
static inline unsigned int life_spark_length_us(int setting) {
    if (setting <= 0) return 0;
    if (setting > 100) setting = 100;
    return 60000u + (unsigned int)setting * 2400u;   /* 62.4ms .. 300ms */
}
