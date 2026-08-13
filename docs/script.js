// ==========================================================================
// Anastasia Engine v7.1 - Interactive Web Engine & Dynamic UI Logic
// ==========================================================================

document.addEventListener('DOMContentLoaded', () => {
    // 1. FAQ Search Filter Logic
    const faqSearchInput = document.getElementById('faqSearchInput');
    const faqItems = document.querySelectorAll('.faq-item');

    if (faqSearchInput) {
        faqSearchInput.addEventListener('input', (e) => {
            const query = e.target.value.toLowerCase().trim();
            faqItems.forEach(item => {
                const question = item.querySelector('.faq-question').textContent.toLowerCase();
                const answer = item.querySelector('.faq-answer').textContent.toLowerCase();
                
                if (question.includes(query) || answer.includes(query)) {
                    item.style.display = 'block';
                } else {
                    item.style.display = 'none';
                }
            });
        });
    }

    // 2. FAQ Accordion Toggle
    faqItems.forEach(item => {
        const questionBtn = item.querySelector('.faq-question');
        questionBtn.addEventListener('click', () => {
            const isActive = item.classList.contains('active');
            
            // Close all items
            faqItems.forEach(i => i.classList.remove('active'));
            
            // Toggle clicked item
            if (!isActive) {
                item.classList.add('active');
            }
        });
    });

    // 3. Code Explorer Tab Switcher
    const codeTabBtns = document.querySelectorAll('.code-tab-btn');
    const codeDisplay = document.getElementById('codeDisplay');

    const codeSnippets = {
        pi_spigot: `.fn compute_pi_spigot(p0: ptr, p1: i64) -> i64
    .registers 6 local

    ; p0 = Memory Buffer, p1 = Digits Count (100)
    move-const v0, 0          ; Digit counter i = 0
    move-const v1, 10         ; Base multiplier

spigot_loop:
    if-ge v0, p1, spigot_end
    mul-int/64 v2, v0, v1
    add-int/64 v3, p0, v2
    store-mem [v3 + 0], v2    ; Store high-precision digit term
    add-int/64 v0, v0, 1
    goto spigot_loop

spigot_end:
    sink-mem p0
    return-val p1              ; Returns 100 Digits Output Checksum = 434
.end_fn`,

        io_uring: `.fn async_network_recv(p0: ptr, p1: i64) -> i64
    .registers 4 local

    ; p0 = Pointer to io_uring ring buffer
    ; p1 = Socket File Descriptor (fd)

    ; Submit hardware async receive to kernel SQ ring
    io-submit [p0 + 0], 1       ; 1 = IORING_OP_RECV

poll_loop:
    io-poll [p0 + 16]           ; Poll kernel CQ ring buffer
    move-const v0, 0
    if-eq v0, 0, poll_loop

    load-mem v1, [p0 + 32]       ; Packet payload pointer
    sink-mem v1
    move-const v2, 200          ; HTTP 200 OK
    return-val v2
.end_fn`,

        exceptions: `.fn divide_safe(p0: i64, p1: i64) -> i64
    .registers 4 local

.try
    if-eq p1, 0, throw_err
    div-int/64 v0, p0, p1
    goto try_end

throw_err:
    move-const v1, 404          ; Error Code 404 (Division by Zero)
    throw-exception v1           ; Bare-Metal Stack Unwinder

.catch DivisionByZeroException
    move-const v0, -1           ; Fallback result
    goto try_end

try_end:
    return-val v0
.end_fn`,

        autovectorizer: `.fn autovectorized_add(p0: ptr, p1: ptr, p2: i64) -> void
    .registers 2 local

    move-const v0, 0
vec_loop:
    if-ge v0, p2, vec_end
    ; SSA pass automatically autovectorizes this to EVEX 512-bit ZMM instructions!
    add-vector/i32x4 p0, p0, p1
    add-int/64 v0, v0, 1
    goto vec_loop
vec_end:
    return-void
.end_fn`,

        atomics: `.fn increment_atomic_counter(p0: ptr, p1: i64) -> i64
    .registers 1 local
    atomic-add/64 [p0 + 0], p1
    fence                       ; Full hardware memory barrier (mfence / sfence)
    load-mem v0, [p0 + 0]
    return-val v0
.end_fn`
    };

    if (codeTabBtns && codeDisplay) {
        codeTabBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                codeTabBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                
                const snippetKey = btn.dataset.snippet;
                if (codeSnippets[snippetKey]) {
                    codeDisplay.textContent = codeSnippets[snippetKey];
                }
            });
        });
    }

    // 4. Copy-to-Clipboard Helper
    const copyBtns = document.querySelectorAll('.copy-btn');
    copyBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            let textToCopy = '';
            if (btn.dataset.copyTarget) {
                textToCopy = document.querySelector(btn.dataset.copyTarget).textContent;
            } else if (btn.parentElement.querySelector('.command')) {
                textToCopy = btn.parentElement.querySelector('.command').textContent;
            } else if (btn.parentElement.parentElement.querySelector('code')) {
                textToCopy = btn.parentElement.parentElement.querySelector('code').textContent;
            }
                
            if (textToCopy) {
                navigator.clipboard.writeText(textToCopy).then(() => {
                    const originalText = btn.innerHTML;
                    btn.innerHTML = '✓ Copied!';
                    setTimeout(() => {
                        btn.innerHTML = originalText;
                    }, 2000);
                });
            }
        });
    });
});
