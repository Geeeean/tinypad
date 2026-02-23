#include "components/nav_bar.h"
#include "components/style.h"

#define SCREEN_COUNT 2
const char *SCREEN_NAMES[SCREEN_COUNT] = {"CONFIG", "MIXER"};

int UpdateAndDrawTextButtons(Font font, const char **strings, int count,
                             float first_slot_center_x, float slot_width, float start_y,
                             float font_size, float letter_spacing, Color default_color,
                             Color hover_color, Color active_color, Screen screen)
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
        if ((int)screen == i) {
            current_color = active_color;
        } else if (is_hovered) {
            current_color = hover_color;
        }

        DrawTextEx(font, strings[i], text_pos, font_size, letter_spacing, current_color);

        // DrawRectangleLinesEx(hit_box, 1.0f, RED);
    }

    return clicked_index;
}

void DrawNavBar(int *y, Screen *screen)
{
    int screen_w = GetScreenWidth();

    float slot_width = 120.0f;
    float total_menu_width = (SCREEN_COUNT - 1) * slot_width;
    float start_x = ((float)screen_w / 2.0f) - (total_menu_width / 2.0f);

    int result =
        UpdateAndDrawTextButtons(font, SCREEN_NAMES, SCREEN_COUNT, start_x, slot_width,
                                 *y, 24.0f, 1.5f, GRAY, ITEM_FOREGROUND, YELLOW, *screen);
    if (result >= 0) {
        *screen = result;
    }

    char *logo = "VELVET";
    float logo_font_size = 30.0f;
    float spacing = 2.0;
    Vector2 logo_text_size = MeasureTextEx(font_logo, logo, logo_font_size, spacing);
    DrawTextEx(font_logo, logo,
               (Vector2){.x = WINDOW_PADDING, .y = *y + 5 - (logo_text_size.y / 2)},
               logo_font_size, spacing, ITEM_FOREGROUND);

    const char *fps = TextFormat("%i FPS", GetFPS());
    int fps_size = MeasureText(fps, 20.0);
    DrawText(fps, screen_w - 10 - fps_size, 10, 20, LIME);

    *y += 40;
}
