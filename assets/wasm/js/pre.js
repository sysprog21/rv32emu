Module['noInitialRun'] = true;
Module['onRuntimeInitialized'] = function(target_elf) {
    if(target_elf === undefined){
      console.warn("target elf executable is undefined");
      return;
    }

    callMain([target_elf]);
};
