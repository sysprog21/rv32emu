Module["noInitialRun"] = true;

Module["run_system"] = function (cli_param) {
  callMain(cli_param.split(" "));
};

// index.html's preRun needs to access this, thus declaring as global
let term;

Module["onRuntimeInitialized"] = function () {
  const input_buf_ptr = Module._get_input_buf();
  const input_buf_cap = Module._get_input_buf_cap();

  term = new Terminal({
    cols: 120,
    rows: 11,
  });
  term.open(document.getElementById("terminal"));

  term.onKey(({ key, domEvent }) => {
    const code = key.charCodeAt(0);
    let sequence;

    switch (domEvent.key) {
      case "ArrowUp":
        // ESC [ A → "\x1B[A"
        sequence = "\x1B[A";
        break;
      case "ArrowDown":
        // ESC [ B → "\x1B[B"
        sequence = "\x1B[B";
        break;
      case "ArrowRight":
        // ESC [ C → "\x1B[C"
        sequence = "\x1B[C";
        break;
      case "ArrowLeft":
        // ESC [ D → "\x1B[D"
        sequence = "\x1B[D";
        break;
      // TODO: support more escape keys?
      default:
        sequence = key;
        break;
    }

    let heap = new Uint8Array(
      Module.HEAPU8.buffer,
      input_buf_ptr,
      sequence.length,
    );

    for (let i = 0; i < sequence.length && i < input_buf_cap; i++) {
      heap[i] = sequence.charCodeAt(i);
    }
    // Fill zero
    for (let i = sequence.length; i < input_buf_cap; i++) {
      heap[i] = 0;
    }

    Module._set_input_buf_size(sequence.length);

    term.scrollToBottom();
  });
};
