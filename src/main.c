#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <arm_neon.h>
#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <time.h>
#include "./constants.h"

int last_frame_time = 0;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

typedef struct {
    uint32_t scale_factor;
    uint32_t window_width;
    uint32_t window_height;
} config_t;

typedef struct {
    uint16_t opcode;
    uint16_t NNN; // 12 bit address/constant
    uint16_t NN; // 8 bit address/constat
    uint16_t N; // 4 bit constant
    uint16_t X; // 4 bit register identifyer
    uint16_t Y; // 4 bit register identifyer
    uint16_t reg_i; // 12 bit register
} instruction_t;

typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32]; // display = &ram[0xF00];
    uint16_t stack[12]; // subroutine stack;
    uint16_t *stack_ptr;
    uint16_t PC; // program counter
    uint8_t V[16]; // Data registers V0-VF;
    uint8_t delay_timer; // Decrements at 60Hz when > 0;
    uint8_t sound_timer;
    bool keypad[16]; // Hexadecimal keypad
    const char *rom_name; // Currently running rom
    instruction_t inst; // Currently executing instruction
} chip8_t;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

int initialize_window(sdl_t *sdl, config_t *config) {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
          fprintf(stderr, "Error initializing SDL");
          return false;
      };
      sdl->window = SDL_CreateWindow(
          "CHIP8EMU",
          SDL_WINDOWPOS_CENTERED,
          SDL_WINDOWPOS_CENTERED,
          config->window_width * config->scale_factor,
          config->window_height * config->scale_factor,
          SDL_WINDOW_SHOWN);
      if (!sdl->window) {
          fprintf(stderr, "Error creating SDL object\n");
          return false;
      }
      sdl->renderer = SDL_CreateRenderer(sdl->window, 0, 0); // -1 - default, 0 - flags
      if (!sdl->renderer) {
          fprintf(stderr, "Error creating renderer\n");
          return false;
      }
      SDL_SetRenderDrawColor(sdl->renderer, 255, 255, 0, 255);
      SDL_RenderClear(sdl->renderer);
      SDL_RenderPresent(sdl->renderer);
    return true;
}

void handle_input(chip8_t *chip8) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT: // This event triggers whenever you click ESC button.
                printf("esc");
                chip8->state = QUIT;
                return;

            case SDL_KEYDOWN: // when some key is down
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    chip8->state = QUIT;
                    return;

                case SDLK_SPACE:
                    if (chip8->state==RUNNING) {
                        chip8->state = PAUSED;
                        puts("===== PAUSED =====");
                    } else {
                        chip8->state = RUNNING;
                    }
                    break;
                    // Map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break;

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break;

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break;

                    case SDLK_z: chip8->keypad[0xA] = true; break;
                    case SDLK_x: chip8->keypad[0x0] = true; break;
                    case SDLK_c: chip8->keypad[0xB] = true; break;
                    case SDLK_v: chip8->keypad[0xF] = true; break;

                default:
                    break;
            }
            break;
            case SDL_KEYUP:
                            switch (event.key.keysym.sym) {
                                // Map qwerty keys to CHIP8 keypad
                                case SDLK_1: chip8->keypad[0x1] = false; break;
                                case SDLK_2: chip8->keypad[0x2] = false; break;
                                case SDLK_3: chip8->keypad[0x3] = false; break;
                                case SDLK_4: chip8->keypad[0xC] = false; break;

                                case SDLK_q: chip8->keypad[0x4] = false; break;
                                case SDLK_w: chip8->keypad[0x5] = false; break;
                                case SDLK_e: chip8->keypad[0x6] = false; break;
                                case SDLK_r: chip8->keypad[0xD] = false; break;

                                case SDLK_a: chip8->keypad[0x7] = false; break;
                                case SDLK_s: chip8->keypad[0x8] = false; break;
                                case SDLK_d: chip8->keypad[0x9] = false; break;
                                case SDLK_f: chip8->keypad[0xE] = false; break;

                                case SDLK_z: chip8->keypad[0xA] = false; break;
                                case SDLK_x: chip8->keypad[0x0] = false; break;
                                case SDLK_c: chip8->keypad[0xB] = false; break;
                                case SDLK_v: chip8->keypad[0xF] = false; break;

                                default: break;
                            }
                break;
            default:
                break;
        }
    }
    }


void close_window(sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

bool init_chip8(chip8_t *chip8, const char rom_name[]) {
    const uint32_t entry_point = 0x200; // CHIP8 roms will be loaded to 0x200
    chip8->state = RUNNING;
    chip8->PC = entry_point; // Start PC at ROM entry point
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    // Load font
    const uint32_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    memcpy(&chip8->ram[0], font, sizeof(font));
    // Load rom
    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("Rom file %s is invalid", rom_name);
        return false;
    };
    // Set chip8 machine defaults
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);
    if (rom_size > max_size) {
        SDL_Log("Rom size %zu is too big.", rom_size);
        return false;
    }
    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
        SDL_Log("Could not read rom file into chip memory");
        return false;
    };
    fclose(rom);
    return true; // Success
}

void update_screen(const sdl_t *sdl, chip8_t *chip8, config_t *config) {
        SDL_Rect rect = {.x=0, .y=0, .w = config->scale_factor, .h = config->scale_factor};
        // Loop through display pixels, draw a rectangle per pixel to the SDL window
        for (uint32_t i = 0; i < sizeof chip8->display; i++) {
            // Translate 1D index i value to 2D X/Y coords
            rect.x = (i % config->window_width) * config->scale_factor;
            rect.y = (i / config->window_width) * config->scale_factor;
            // If pixel is on
            if (chip8->display[i]) {
                SDL_SetRenderDrawColor(sdl->renderer, 255, 255, 255, 255);
                SDL_RenderFillRect(sdl->renderer, &rect);
            } else {
                // If it's off
                SDL_SetRenderDrawColor(sdl->renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(sdl->renderer, &rect);
            }
        }
        SDL_RenderPresent(sdl->renderer);
}

typedef void (*MachineCodeRoutine)();

void executeMachineCodeRoutine(chip8_t *chip8) {
    // Assuming machine code routines are stored in the chip8->ram array
    MachineCodeRoutine routine = (MachineCodeRoutine)&chip8->inst.NNN;
    routine(); // Call the machine code routine
}


void emulate_commands(chip8_t *chip8, config_t *config) {
    // Get next opcode from ram
    chip8->inst.opcode = chip8->ram[chip8->PC] << 8 | chip8->ram[chip8->PC+1]; // Reading 2 bytes and combining them
    chip8->PC += 2; // Pre-increment program counter for next opcode
    // Fill out current instruction format
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF; // 0FFF is 4096; address
    chip8->inst.NN = chip8->inst.opcode & 0x0FF; // 255;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;
    // Emulate opcode
    printf("Address: 0x%04X Opcode: 0x%04X, desc: ", chip8->PC - 2, chip8->inst.opcode);
    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                // 0x00E0: Clear screen
                printf("Clear screen\n");
                memset(&chip8->display[0], false, sizeof chip8->display);
            } else if (chip8->inst.NN == 0xEE) {
                // 0x00EE: Return from subroutine
                // Set program counter to last address on subroutine stack ("pop" it off the stack)
                //   so that next opcode will be gotten from that address.
                printf("Return from subroutine\n");
                chip8->PC = *--chip8->stack_ptr;
            } else {
                // Calls machine code routine (RCA 1802 for COSMAC VIP) at address NNN
                executeMachineCodeRoutine(chip8);
            }
            break;
        case 0x01:
            // Jump to NNN
            printf("Jump to NNN (0x%04X)\n", chip8->inst.NNN);
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x02:
            // Calls subroutine at NNN.
            printf("Calls subroutine at NNN.");
            *chip8->stack_ptr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x03:
            printf("Skips the next instruction if VX equals NN\n");
            if (chip8->V[chip8->inst.X] == chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;
        case 0x04:
            printf("Skips the next instruction if VX NOT equals NN\n");
            if (chip8->V[chip8->inst.X] != chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;
        case 0x05:
            printf("Skips the next instruction if VX equals VY\n");
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) {
                chip8->PC += 2;
            }
            break;
        case 0x06:
            // 0x6XNN - Set register VX to NN
            printf("Set register V%X(0x%04X) to NN (0x%04x)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            printf("Adds NN (%04X) to VX (%04X)\n", chip8->V[chip8->inst.X], chip8->inst.NN);
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch (chip8->inst.N) {
                case 0x0:
                    printf("Sets VX (%04X) to the value of VY (%04X).", chip8->inst.X, chip8->inst.Y);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 0x1:
                    // Sets VX to VX OR VY
                    printf("Sets VX (%04X) to VX or VY (%04X).", chip8->inst.X, chip8->inst.Y);
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    break;
                case 0x2:
                    printf("Sets VX (%04X) to VX and VY (%04X).", chip8->inst.X, chip8->inst.Y);
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    break;
                case 0x3:
                    // Sets VX to VX xor VY
                    printf("Sets VX (%04X) to VX xor VY (%04X).", chip8->inst.X, chip8->inst.Y);
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    break;
                case 0x4:
                    printf("Adds VY (%04X) to VX (%04X).", chip8->inst.Y, chip8->inst.X);
                    chip8->inst.X += chip8->inst.Y;
                    break;
                case 0x5:
                    printf("Substract VY (%04X) from VX (%04X).", chip8->inst.Y, chip8->inst.X);
                    chip8->inst.X -= chip8->inst.Y;
                    break;
                case 0x6:
                    // Stores the least significant bit of VX in VF and then shifts VX to the right by 1.
                    printf("Stores the least significant bit of VX (%04X) in VF.", chip8->inst.X);
                    int LSB = chip8->V[chip8->inst.X] & 0x01; // least significant bit of VX
                    chip8->V[chip8->inst.X] >>= 1;
                    break;
                case 0x7:
                    // Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not.
                    printf("Sets VX (%04X) to VY (%04X) minus VX.", chip8->inst.X, chip8->inst.Y);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    if (chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X]) {
                        chip8->V[15] = 1;
                    } else {
                        chip8->V[15] = 0;
                    }
                    break;
                case 0xE:
                    // Stores the most significant bit of VX in VF and then shifts VX to the left by 1.
                    printf("Stores the most significant bit of VX (%04X) in VF.", chip8->inst.X);
                    int MSB = chip8->V[chip8->inst.X] | 0x01; // least significant bit of VX
                    chip8->V[chip8->inst.X] <<= 1;
                    break;
            }
        case 0x09:
            printf("Skips the next instruction if VX NOT equals VY\n");
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
                chip8->PC += 2;

            break;
        case 0x0A:
            printf("Sets I (0x%04X) to the address NNN (0x%04X)\n", chip8->inst.reg_i, chip8->inst.NNN);
            chip8->inst.reg_i = chip8->inst.NNN;
            break;
        case 0x0B:
            //Jumps to the address NNN plus V0.
            printf("Jumps to the address NNN plus V0\n");
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;
        case 0x0C:
            //Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.
            printf("Sets VX to the result of a bitwise and operation on a random numbe.\n");
            srand(time(NULL));   // Initialization, should only be called once.
            int r = rand();
            chip8->V[chip8->inst.X] = r & chip8->inst.NN;
            break;
        case 0x0D:
            // 0xDXYN - Draw N-height sprite at coords X, Y; Read from memory location I
            // Screen pixels are XOR'd with sprite bits,
            // VX (Carry flag) is set if any screen pixels are set off; This is usefult for
            // collision detection or other reasons
            printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) from memory location I (0x%04X).\n",
                chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->inst.reg_i);
                uint8_t x_coord = chip8->V[chip8->inst.X] % config->window_width;
                uint8_t y_coord = chip8->V[chip8->inst.Y] % config->window_height;
                const uint8_t original_x = x_coord;
                chip8->V[0xF] = 0; // Initialize carry flag to 0
                // Loop over all N rows of the sprite
                for (uint8_t i = 0; i < chip8->inst.N; i++) {
                    // Get next byte/row of sprite data
                    const uint8_t sprite_data = chip8->ram[chip8->inst.reg_i + i];
                    x_coord = original_x; // Reset X for next raw draw

                    for (int8_t j = 7; j >= 0; j--) {
                        // If sprite pixel/bit is on and display pixel is on, set carry flag
                        bool *pixel = &chip8->display[y_coord * config->window_width + x_coord];
                        const bool sprite_bit = (sprite_data & (1 << j));
                        if (sprite_bit && *pixel) {
                            chip8->V[0xF] = 1;
                        }
                        // XOR display pixel with sprite pixel/bit
                        *pixel ^= sprite_bit;
                        // Stop drawing if hit right edge of screen
                        if (++x_coord >= config->window_width *config->scale_factor) break;
                    }
                    // Stop drawing entire sprite if hit bottom edge of screen
                    if (++y_coord >= config->window_height * config->scale_factor) break;
                }
                break;
            case 0x0E:
                       if (chip8->inst.NN == 0x9E) {
                           // 0xEX9E: Skip next instruction if key in VX is pressed
                           printf("Skip next instruction if key in VX is pressed");
                           if (chip8->keypad[chip8->V[chip8->inst.X]])
                               chip8->PC += 2;

                       } else if (chip8->inst.NN == 0xA1) {
                           printf("Skip next instruction if key in VX is not pressed");
                           // 0xEX9E: Skip next instruction if key in VX is not pressed
                           if (!chip8->keypad[chip8->V[chip8->inst.X]])
                               chip8->PC += 2;
                       }
                       break;
            case 0x0F:
                switch (chip8->inst.NN) {
                    case 0x0A: {
                        // A key press is awaited, and then stored in VX
                        printf("A key press is awaited, and then stored in VX");
                        static bool any_key_pressed = false;
                        static uint8_t key = 0xFF;
                        for (uint8_t i = 0; key == 0xFF && i < sizeof chip8->keypad; i++) {
                            if (chip8->keypad[i]) {
                                key = i;
                                any_key_pressed = true;
                                break;
                            }
                            if (!any_key_pressed) {
                                chip8->PC -= 2;
                            } else {
                                if (chip8->keypad[key]) {
                                    chip8->PC -= 2;
                                } else {
                                    chip8->V[chip8->inst.X] = key;
                                    key = 0xFF;
                                    any_key_pressed = false;
                                }
                            }
                            break;
                        }
                    }
                    case 0x07:
                        // Sets VX to the value of the delay timer.
                        printf("Sets VX to the value of the delay timer.");
                        chip8->V[chip8->inst.X] = chip8->delay_timer;
                        break;
                    case 0x15:
                        // Sets the delay timer to VX
                        printf("Sets the delay timer to VX");
                        chip8->delay_timer = chip8->V[chip8->inst.X];
                        break;
                    case 0x18:
                        // Sets the sound timer to VX.
                        printf("Sets the sound timer to VX.");
                        chip8->sound_timer = chip8->V[chip8->inst.X];
                        break;
                    case 0x1E:
                        // Adds VX to I. VF is not affected.
                        printf("Adds VX to I. VF is not affected.");
                        chip8->inst.reg_i += chip8->V[chip8->inst.X];
                        break;
                    case 0x29:
                        // Sets I to the location of the sprite for the character in VX.
                        printf("Sets I to the location of the sprite for the character in VX.");
                        chip8->inst.reg_i = chip8->V[chip8->inst.X] * 5;
                        break;
                    case 0x33: {
                        printf("Stores the binary-coded decimal representation of VX");
                        // Stores the binary-coded decimal representation of VX,
                        // with the hundreds digit in memory at location in I,
                        // the tens digit at location I+1, and the ones digit at location I+2
                        uint8_t bcd = chip8->V[chip8->inst.X];
                        chip8->ram[chip8->inst.reg_i+2] = bcd % 10;
                        bcd /= 10;
                        chip8->ram[chip8->inst.reg_i+1] = bcd % 10;
                        bcd /= 10;
                        chip8->ram[chip8->inst.reg_i] = bcd;
                        break;
                case 0x55:
                printf("Stores from V0 to VX (including VX) in memory, starting at address I.");
                for (uint8_t i = 0; i <= chip8->inst.X; i++)  {
                    chip8->ram[chip8->inst.reg_i++] = chip8->V[i]; // Increment I each time
                    }
                    break;
                case 0x65:
                printf("Fills from V0 to VX (including VX) with values from memory, starting at address I.");
                // 0xFX65: Register load V0-VX inclusive from memory offset from I;
                //   SCHIP does not increment I, CHIP8 does increment I
                for (uint8_t i = 0; i <= chip8->inst.X; i++) {
                    chip8->V[i] = chip8->ram[chip8->inst.reg_i++]; // Increment I each time
                    break;
                }

        default:
            break;
                    }
                }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    sdl_t sdl = {0};
    chip8_t chip8 = {0};
    config_t config = {0};
    config.window_width = 64;
    config.window_height = 32;
    config.scale_factor = 10;
    const char *rom_name = argv[1];
    init_chip8(&chip8, rom_name);
    initialize_window(&sdl, &config);
    while (chip8.state != QUIT) {
        handle_input(&chip8);
        if (chip8.state == PAUSED) continue;
        emulate_commands(&chip8, &config);
        SDL_Delay(16);
        update_screen(&sdl, &chip8, &config);
    }
    close_window(sdl);
    return 0;
}
