/*
 * Copyright 2016 Frank Hunleth
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "progress.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <sys/time.h>

// Elapsed time measurement maxes out at 2^31 ms = 24 days
// Despite not being monotic, gettimeofday is more portable, and
// getting the duration wrong is not the end of the world.
static int current_time_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static void output_progress(struct fwup_progress *progress, int percent)
{
    if (percent == progress->last_reported)
        return;

    progress->last_reported = percent;

    switch (progress->mode) {
    case PROGRESS_MODE_NUMERIC:
        printf("%d\n", percent);
        break;

    case PROGRESS_MODE_NORMAL:
        printf("\r%3d%%", percent);
        fflush(stdout);
        break;

    case PROGRESS_MODE_FRAMING:
        fwup_output(FRAMING_TYPE_PROGRESS, percent, "");
        break;

    case PROGRESS_MODE_OFF:
    default:
        break;
    }
}

/**
 * @brief Initialize progress reporting
 *
 * This function also immediately outputs 0% progress so that the user
 * gets feedback as soon as possible.
 */
void progress_init(struct fwup_progress *progress, enum fwup_progress_mode mode)
{
    progress->mode = mode;
    progress->last_reported = -1;
    progress->total_units = 0;
    progress->current_units = 0;
    progress->start_time = 0;

    output_progress(progress, 0);
}

/**
 * @brief Call this to report progress.
 *
 * @param progress the progress info
 * @param units the number of "progress" units to increment
 */
void progress_report(struct fwup_progress *progress, int units)
{
    // Start the timer once we start for real
    if (progress->mode == PROGRESS_MODE_NORMAL &&
            progress->start_time == 0 &&
            progress->total_units > 0)
        progress->start_time = current_time_ms();

    progress->current_units += units;
    assert(progress->current_units <= progress->total_units);

    int percent;
    if (progress->total_units) {
        percent = (int) (progress->current_units * 100 / progress->total_units);

        // Don't report 100% until the very, very end just in case something takes
        // longer than expected in the code after all progress units have been reported.
        if (percent > 99)
            percent = 99;
    } else {
        percent = 0;
    }

    output_progress(progress, percent);
}

/**
 * @brief Call this when the operation is 100% complete
 *
 * @param progress the progress info
 */
void progress_report_complete(struct fwup_progress *progress)
{
    output_progress(progress, 100);

    switch (progress->mode) {
    case PROGRESS_MODE_NORMAL:
        if (progress->start_time) {
            int elapsed = current_time_ms() - progress->start_time;
            printf("\nElapsed time: %d.%03ds\n",
               elapsed / 1000,
               elapsed % 1000);
        }
        break;

    case PROGRESS_MODE_NUMERIC:
    case PROGRESS_MODE_FRAMING:
    case PROGRESS_MODE_OFF:
    default:
        break;
    }
}
