'use strict';

// Store only display preferences; no browsing data leaves this page.
document.addEventListener('DOMContentLoaded', () => {
    for (const input of document.querySelectorAll('.customize-panel input')) {
        try {
            const key = `savana-start-${input.id}`;
            const legacyKey = `savanna-start-${input.id}`;
            const saved = localStorage.getItem(key) ?? localStorage.getItem(legacyKey);
            if (saved !== null)
                input.checked = saved === 'true';
            if (saved !== null && localStorage.getItem(key) === null) {
                localStorage.setItem(key, saved);
                localStorage.removeItem(legacyKey);
            }
        } catch {
            // Storage may be unavailable in an ephemeral browsing context.
        }

        input.addEventListener('change', () => {
            try {
                localStorage.setItem(`savana-start-${input.id}`, String(input.checked));
            } catch {
                // The controls still work for the current page without storage.
            }
        });
    }
});
