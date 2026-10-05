import sys
import time
from playwright.sync_api import sync_playwright

def test_stability():
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1280, "height": 900})

        logs = []
        errors = []
        page.on("console", lambda m: logs.append(f"[{m.type}] {m.text}"))
        page.on("pageerror", lambda e: errors.append(str(e)))

        print("--- Testing tactical_terminal.html ---")
        page.goto("http://127.0.0.1:8080/tactical_terminal.html")
        page.wait_for_timeout(1000)

        # Switch to Map page
        page.click("#keyPage")
        page.wait_for_timeout(1000)

        active = page.evaluate("() => document.querySelector('.garmin-page.active-page')?.id")
        assert active == "pageMap", f"Expected pageMap, got {active}"

        # Check tiles
        tiles_count = page.evaluate("() => document.querySelectorAll('#garminMapCanvas img.leaflet-tile').length")
        print(f"OSM Topo tiles loaded: {tiles_count}")
        assert tiles_count > 0, "No tiles loaded for OSM Topo!"

        # Cycle layers: Satellite -> NVG Dark -> Basemap -> OSM Topo
        for layer_name in ["BIRDSEYE", "NVG DARK", "BASEMAP", "OSM TOPO"]:
            page.click("#btnToggleMapLayer")
            page.wait_for_timeout(600)
            current_label = page.evaluate("() => document.getElementById('mapLayerLabel')?.textContent")
            print(f"Layer toggled to: {current_label}")
            assert current_label == layer_name, f"Expected {layer_name}, got {current_label}"

        # Drag map to test Pan Mode
        canvas = page.locator("#garminMapCanvas")
        box = canvas.bounding_box()
        page.mouse.move(box["x"] + box["width"]/2, box["y"] + box["height"]/2)
        page.mouse.down()
        page.mouse.move(box["x"] + box["width"]/2 + 80, box["y"] + box["height"]/2 + 80, steps=8)
        page.mouse.up()
        page.wait_for_timeout(500)

        is_pan = page.evaluate("() => isPanMode")
        crosshair_display = page.evaluate("() => document.getElementById('garminPanCrosshair')?.style.display")
        hud_display = page.evaluate("() => document.getElementById('garminPanHud')?.style.display")
        print(f"Pan Mode active: {is_pan}, Crosshair: {crosshair_display}, HUD: {hud_display}")
        assert is_pan is True, "Pan Mode did not activate on drag!"
        assert crosshair_display == "block", "Crosshair not shown in pan mode!"

        # Cycle pages away and back to test layout persistence
        print("Cycling through all pages...")
        for _ in range(6):
            page.click("#keyPage")
            page.wait_for_timeout(200)

        active = page.evaluate("() => document.querySelector('.garmin-page.active-page')?.id")
        assert active == "pageMap", f"Expected pageMap after full loop, got {active}"

        tiles_after = page.evaluate("() => document.querySelectorAll('#garminMapCanvas img.leaflet-tile').length")
        print(f"Tiles after cycling pages: {tiles_after}")
        assert tiles_after > 0, "Tiles disappeared after cycling pages!"

        # Test Recenter button
        page.click("button:has-text('MERKEZLE')")
        page.wait_for_timeout(500)
        is_pan_after = page.evaluate("() => isPanMode")
        print(f"Pan Mode after Recenter: {is_pan_after}")
        assert is_pan_after is False, "Recenter did not turn off pan mode!"

        # Take screenshot of final tactical map
        page.screenshot(path="scratch/tactical_terminal_final_check.png")
        print("Tactical terminal check passed!")

        print("\n--- Testing gnss_tracker.html ---")
        page.goto("http://127.0.0.1:8080/gnss_tracker.html")
        page.wait_for_timeout(1000)

        # Drag map in tracker
        t_map = page.locator("#map")
        t_box = t_map.bounding_box()
        page.mouse.move(t_box["x"] + t_box["width"]/2, t_box["y"] + t_box["height"]/2)
        page.mouse.down()
        page.mouse.move(t_box["x"] + t_box["width"]/2 + 150, t_box["y"] + t_box["height"]/2 + 150, steps=10)
        page.mouse.up()
        page.wait_for_timeout(800)

        follow_state = page.evaluate("() => followMap")
        follow_text = page.evaluate("() => document.getElementById('btnFollowText')?.textContent")
        print(f"Follow state after drag: {follow_state} ({follow_text})")
        assert follow_state is False, "Drag did not auto-disable followMap!"
        assert "KAPALI" in follow_text, f"Button text expected KAPALI, got {follow_text}"

        page.screenshot(path="scratch/gnss_tracker_final_check.png")
        print("GNSS tracker check passed!")

        print("\nConsole errors:", errors)
        assert len(errors) == 0, f"Found JavaScript errors: {errors}"
        print("ALL STABILITY TESTS PASSED WITH 0 ERRORS!")
        browser.close()

if __name__ == '__main__':
    test_stability()
