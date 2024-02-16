#include "../raylib/src/raylib.h"
#include "../raylib/src/raymath.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CONST_G 0.01f

struct thread_args_t {
    // Indices for the interval to update
    size_t from;
    size_t to;
    size_t total;

    // latest updated positions, currently being drawn
    Vector2 *new_positions;
    // previous positions, needs to be updated
    Vector2 *positions;

    float *masses;
    Vector2 *speed;
    Vector2 *acc;
};

// Function to update the position of the bodies
// The result is an updated @positions[from..to] and @speed[from..to]
// Notes about synchronisation :
// There is not really any need for synchronisation, the values
// that get updated are withing the [from..to] range, everything
// else is read only.
void *update_positions(void *thread_args) {
    struct thread_args_t *args = thread_args;
    // load arguments into typed variables
    float current_x, current_y, other_x, other_y;
    float dir_x, dir_y;
    float g, d, d_x, d_y, d_sum;

    int from = args->from, to = args->to, total = args->total;

    for (size_t i = from; i <= to; i++) {
        args->acc[i].x = 0;
        args->acc[i].y = 0;
        current_x = args->new_positions[i].x;
        current_y = args->new_positions[i].y;

        for (size_t j = 0; j < total; j++) {
            if (i == j) {
                continue;
            }
            other_x = args->new_positions[j].x;
            other_y = args->new_positions[j].y;

            d_x = (current_x - other_x) * (current_x - other_x);
            d_y = (current_y - other_y) * (current_y - other_y);
            d = d_x + d_y;

            if (d < 1) {
                d = 1;
                args->speed[i].x *= 0.9990;
                args->speed[i].y *= 0.9990;
            }

            g = (CONST_G * args->masses[j]) / d;
            dir_x = (other_x - current_x);
            dir_y = (other_y - current_y);

            args->acc[i].x += dir_x * g;
            args->acc[i].y += dir_y * g;
        }
        args->speed[i].x += args->acc[i].x;
        args->speed[i].y += args->acc[i].y;

        args->positions[i].x = current_x + args->speed[i].x;
        args->positions[i].y = current_y + args->speed[i].y;
    }
}

// Readable version for reference
void *REFERENCE_update_positions(void *thread_args) {
    struct thread_args_t *args = thread_args;
    float d;
    Vector2 dir, final;

    for (size_t i = args->from; i <= args->to; i++) {

        // we do not want inertia on the acceleration
        args->acc[i] = Vector2Zero();

        for (size_t j = 0; j < args->total; j++) {
            // No self interraction
            if (i == j) {
                continue;
            }
            // Distance between [i] and [j]
            d = Vector2Distance(args->new_positions[i], args->new_positions[j]);

            // To avoid weird accelerations when two elements are very close
            if (d < 1) {
                d = 1;
                args->speed[i] = Vector2Scale(args->speed[i], 0.9990);
            }
            // The substraction gives us the force vector
            dir = Vector2Subtract(args->new_positions[j], args->new_positions[i]);

            // Using a tuned gravitation formula
            // In the optimized version I skip both the sqrt() and the squaring
            final = Vector2Scale(dir, (CONST_G * args->masses[j]) / (d * d));

            args->acc[i] = Vector2Add(args->acc[i], final);
        }
        args->speed[i] = Vector2Add(args->speed[i], args->acc[i]);
        args->positions[i] = Vector2Add(args->new_positions[i], args->speed[i]);
    }
}

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void) {
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 800;
    const int nb_bodies = 2000;

    InitWindow(screenWidth, screenHeight, "Many bodies (press Space to start/pause) ");
    SetTargetFPS(60); // Set our game to run at 60 frames-per-second
    bool running = false;

    // Multithreading
    //--------------------------------------------------------------------------------------
    // Boolean for double buffering
    bool buffer_frame = false;
    int nb_threads = 4;
    pthread_t *threads = malloc(nb_threads * sizeof(pthread_t));

    struct thread_args_t *args = malloc(nb_threads * sizeof(struct thread_args_t));

    // Creation of the indices intervals
    // It is critical that the intervals are disjoints.
    Vector2 *sections = malloc(nb_threads * sizeof(Vector2));
    for (size_t i = 0; i < nb_threads; i++) {
        sections[i].x = i * (int)(nb_bodies / nb_threads);
        sections[i].y = (i + 1) * (int)(nb_bodies / nb_threads) - 1;
    }
    // Due to rounding the last index is best added manually
    sections[nb_threads - 1].y = nb_bodies - 1;

    // Bodies data
    //--------------------------------------------------------------------------------------
    // I keep two arrays of positions for double buffering
    Vector2 *pos = malloc(nb_bodies * sizeof(Vector2));
    Vector2 *pos2 = malloc(nb_bodies * sizeof(Vector2));
    // Aliases
    Vector2 *drawn_buffer = pos;
    Vector2 *updated_buffer = pos2;

    // The other values do not need double buffering
    float *masses = malloc(nb_bodies * sizeof(float));
    Vector2 *speed = malloc(nb_bodies * sizeof(Vector2));
    Vector2 *acc = malloc(nb_bodies * sizeof(Vector2));

    SetRandomSeed(time(NULL));

    for (size_t i = 0; i < nb_bodies; i++) {
        // I initialize the positions using polar coordinates to generate a circular
        // cloud instead of the rectangle obtained [x,y]=[rand(), rand()]
        float theta = -PI + ((float)i / (float)nb_bodies) * (2.0f * PI);
        float r = GetRandomValue(0, screenHeight / 2);

        pos[i].x = screenWidth / 2 + r * cos(theta);
        pos[i].y = screenHeight / 2 + r * sin(theta);
        pos2[i].x = pos[i].x;
        pos2[i].y = pos[i].y;

        // ALl the bodies have the same weight
        masses[i] = 1.0;

        // No initial speed : the cloud will collapse on itself
        speed[i] = Vector2Zero();

        // No initial acceleration either
        acc[i] = Vector2Zero();
    }

    // Change one body to be 100x heavier
    masses[0] = 100;

    // Drawing optimisation stuff
    // since all the objects are the same we can pre-render a circle and then re-use it
    // instead of calculating a circle each time
    // During the drawing the top left corner of the canvas is used but it just means
    // every object is drawn a few pixels off
    RenderTexture2D circle_canvas = LoadRenderTexture(5, 5);
    BeginTextureMode(circle_canvas);
    ClearBackground((Color){0, 0, 0, 0}); // Transparent background
    DrawCircle(2.5, 2.5, 2, LIME);
    EndTextureMode();

    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        // Inputs
        //----------------------------------------------------------------------------------
        if (IsKeyPressed(KEY_SPACE)) {
            running = !running;
        }

        // Update
        //----------------------------------------------------------------------------------
        if (running) {

            drawn_buffer = buffer_frame ? pos2 : pos;
            updated_buffer = buffer_frame ? pos : pos2;

            for (size_t _th = 0; _th < nb_threads; _th++) {
                // indices the thread is responsible for
                args[_th].from = sections[_th].x;
                args[_th].to = sections[_th].y;
                args[_th].total = nb_bodies;
                // double buffering, one is updated the other one is drawn
                args[_th].new_positions = drawn_buffer;
                args[_th].positions = updated_buffer;
                // the other arguments that can be shared between all threads
                args[_th].masses = masses;
                args[_th].speed = speed;
                args[_th].acc = acc;
                pthread_create(&threads[_th], NULL, update_positions, &args[_th]);
            }
        }

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

        ClearBackground(BLACK);

        DrawCircleV(pos[0], 10, YELLOW);

        for (int i = 1; i < nb_bodies; i++) {
            DrawTextureV(circle_canvas.texture, drawn_buffer[i], WHITE);
        }

        EndDrawing();
        //----------------------------------------------------------------------------------
        if (running) {
            for (size_t _th = 0; _th < nb_threads; _th++) {
                pthread_join(threads[_th], NULL);
            }
            // buffer swap
            buffer_frame = !buffer_frame;
        }
    }

    CloseWindow();

    free(threads);
    free(args);
    free(sections);
    free(pos);
    free(pos2);
    free(masses);
    free(speed);
    free(acc);

    return 0;
}
