#include <drivers/keyboard.h>
#include <io/ports.h>
#include <kernel/logging.h>
#include <drivers/framebuffer.h>
#include <stddef.h>
#include <sys.h>

static uint8_t current_modifiers, is_pressed, extended_read, buf_pos;
key_status_t keyboard_buffer[128];

char keymap[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
    0, 0 ,0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};
char shift_keymap[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '?', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '|', 0,
    '*', 0, ' ', 0,
    0, 0 ,0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static inline void check_ack(void) {
    if (inb(KEYBOARD_PORT) != KEYBOARD_ACK)
        return log(Error, "KEYBOARD", "Unable to communicate with keyboard");
}

void init_keyboard(void) {
    inb(KEYBOARD_PORT);
    outb(KEYBOARD_PORT, 0xF5); // Disable keyboard from sending scan codes
    check_ack();
    log(Verbose, "KEYBOARD", "Disabled keyboard scan codes");
    outb(KEYBOARD_PORT, 0xF0); // Set scancode set 2
    outb(KEYBOARD_PORT, 2);
    check_ack();
    log(Verbose, "KEYBOARD", "Set keyboard scan code set 2");

    outb(0x64, 0x20);
    while ((inb(PS2_STATUS) & 0b10) != 0);
    uint8_t configuration_byte = inb(PS2_DATA);
    if ((configuration_byte & (1 << 6)) == 0)
        return log(Error, "KEYBOARD", "Translation is not enabled");
    outb(KEYBOARD_PORT, 0xF4);
    check_ack();
    log(Verbose, "KEYBOARD", "Enabled keyboard scan codes");
}

void update_modifiers(key_modifiers_t modifier, int is_pressed) {
    if (is_pressed)
        current_modifiers |= modifier;
    else
        current_modifiers &= ~modifier;
}

key_code_t translate(uint8_t scancode) {
    is_pressed = 1;
    if (scancode == EXTENDED_PREFIX) {
        extended_read = 1;
        return scancode;
    }

    if (scancode & KEY_RELEASE_MASK) {
        is_pressed = 0;
        scancode &= ~((uint8_t) KEY_RELEASE_MASK);
    }

    if (scancode < 0x60) {
        switch (scancode) {
        case LEFT_CTRL:
            update_modifiers(extended_read ? right_ctrl : left_ctrl, is_pressed);
            break;
        case LEFT_ALT:
            update_modifiers(extended_read ? right_alt : left_alt, is_pressed);
            break;
        case LEFT_GUI:
            update_modifiers(left_gui, is_pressed);
            break;
        case RIGHT_GUI:
            update_modifiers(right_gui, is_pressed);
            break;
        case LEFT_SHIFT:
            update_modifiers(left_shift, is_pressed);
            break;
        case RIGHT_SHIFT:
            update_modifiers(right_shift, is_pressed);
            break;
        }
        extended_read = 0;
        return scancode;
    }
    extended_read = 0;
    return 0;
}

char get_char(key_status_t key_status) {
    if (key_status.modifiers & (left_shift | right_shift))
        return shift_keymap[key_status.code];
    return keymap[key_status.code];
}

void handle_keyboard(void) {
    uint8_t scancode = inb(KEYBOARD_PORT);
    extern volatile uint8_t aps_running;
    return log(Verbose, "LAPIC", "%x - %u", *(uint64_t*) VADDR(16 * PAGE_SIZE), aps_running);
    key_code_t translated_scancode = translate(scancode);
    if (scancode == EXTENDED_PREFIX)
        return;
    if (is_pressed == 0)
        return;
    keyboard_buffer[buf_pos].code = translated_scancode;
    keyboard_buffer[buf_pos].modifiers = current_modifiers;
    char key = get_char(keyboard_buffer[buf_pos]);
    if (key != 0) {
        printf("%c", key);
        flush_screen();
    }
}
