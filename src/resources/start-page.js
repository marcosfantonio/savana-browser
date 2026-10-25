'use strict';

// Store only display preferences; no browsing data leaves this page.
document.addEventListener('DOMContentLoaded', () => {
    for (const input of document.querySelectorAll('.customize-panel input')) {
        try {
            const saved = localStorage.getItem(`savanna-start-${input.id}`);
            if (saved !== null)
                input.checked = saved === 'true';
        } catch {
            // Storage may be unavailable in an ephemeral browsing context.
        }

        input.addEventListener('change', () => {
            try {
                localStorage.setItem(`savanna-start-${input.id}`, String(input.checked));
            } catch {
                // The controls still work for the current page without storage.
            }
        });
    }
});
