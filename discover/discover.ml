module C = Configurator.V1

let () =
  C.main ~name:"basement" (fun c ->
    let linux =
      let system = C.ocaml_config_var_exn c "system" in
      (* Possible values for this field: linux, linux_elf, linux_eabi, ... *)
      String.sub system 0 5 = "linux" || String.equal system "elf"
    in
    C.C_define.gen_header_file c ~fname:"config.h" [ "PLATFORM_LINUX", Switch linux ])
;;
