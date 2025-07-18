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

    let heap = new Uint8Array(
      Module.HEAPU8.buffer,
      input_buf_ptr,
      key.length,
    );

    for (let i = 0; i < key.length && i < input_buf_cap; i++) {
      heap[i] = key.charCodeAt(i);
    }
    // Fill zero
    for (let i = key.length; i < input_buf_cap; i++) {
      heap[i] = 0;
    }

    Module._set_input_buf_size(key.length);

    term.scrollToBottom();
  });
};
