Module["noInitialRun"] = true;

Module["run_system"] = function (cli_param) {
  callMain(cli_param.split(" "));
};

// Note: Terminal initialization is defined in system.html
// Module.onRuntimeInitialized is defined in the HTML file
