#include "components/nav_bar.h"
#include "components/style.h"

#define WINDOWS_COUNT 2
const char *WINDOWS_NAMES[WINDOWS_COUNT] = {"CONFIG", "MIXER"};

int UpdateAndDrawTextButtons(Font font, const char **strings, int count,
                             float first_slot_center_x, float slot_width, float start_y,
                             float font_size, float letter_spacing, Color default_color,
                             Color hover_color, Color active_color, Window window)
{
    int clicked_index = -1;
    Vector2 mouse_pos = GetMousePosition();

    for (int i = 0; i < count; i++) {
        Vector2 text_size = MeasureTextEx(font, strings[i], font_size, letter_spacing);

        float current_slot_center_x = first_slot_center_x + (i * slot_width);
        Vector2 text_pos = {current_slot_center_x - (text_size.x / 2.0f), start_y};

        float padding = 5.0f;
        Rectangle hit_box = {text_pos.x - padding, text_pos.y - padding,
                             text_size.x + (padding * 2.0f),
                             text_size.y + (padding * 2.0f)};

        bool is_hovered = CheckCollisionPointRec(mouse_pos, hit_box);

        if (is_hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            clicked_index = i;
        }

        Color current_color = default_color;
        if ((int)window == i) {
            current_color = active_color;
        } else if (is_hovered) {
            current_color = hover_color;
        }

        DrawTextEx(font, strings[i], text_pos, font_size, letter_spacing, current_color);

        // DrawRectangleLinesEx(hit_box, 1.0f, RED);
    }

    return clicked_index;
}

void DrawNavBar(int screen_w, int *y, Window *window)
{

    float slot_width = 120.0f;
    float total_menu_width = (WINDOWS_COUNT - 1) * slot_width;
    float start_x = ((float)screen_w / 2.0f) - (total_menu_width / 2.0f);

    int result =
        UpdateAndDrawTextButtons(font, WINDOWS_NAMES, WINDOWS_COUNT, start_x, slot_width,
                                 *y, 24.0f, 1.5f, GRAY, WHITE, YELLOW, *window);
    if (result >= 0) {
        *window = result;
    }

    *y += 40;
}
