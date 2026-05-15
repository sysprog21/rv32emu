Module["noInitialRun"] = true;

Module["run_user"] = function (target_elf, ...extra_args) {
  if (target_elf === undefined) {
    console.warn("target elf executable is undefined");
    return;
  }

  return callMain([target_elf, ...extra_args]);
};
