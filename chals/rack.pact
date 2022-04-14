class CPU {
  run() {
    var cont = true;
    var code_len = len(this.code);
    while (cont && this.pc < code_len) {
      opcode = this.code[this.pc];
      cont = this.dispatch[opcode]();
    }
  }

  _add() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1];
    this.sp = this.sp - 1;
    this.stack[this.sp] = op1 + op2;
    this.pc = this.pc + 1;
    return true;
  }

  _sub() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1];
    this.sp = this.sp - 1;
    this.stack[this.sp] = op1 - op2;
    this.pc = this.pc + 1;
    return true;
  }

  _xor() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1];
    this.sp = this.sp - 1;
    this.stack[this.sp] = op1 ^ op2;
    this.pc = this.pc + 1;
    return true;
  }

  _copy() {
    var op = this.stack[this.sp];
    this.sp = this.sp + 1;
    this.stack[this.sp] = op;
    this.pc = this.pc + 1;
  }

  _pop() {
    this.sp = this.sp + 1;
    this.pc = this.pc + 1;
    return true;
  }

  _lsl() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1];
    this.sp = this.sp - 1;
    this.stack[this.sp] = op2 << (op1 & 63);
    this.pc = this.pc + 1;
    return true;
  }

  _lsr() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1];
    this.sp = this.sp - 1;
    this.stack[this.sp] = op2 >> (op1 & 63);
    this.pc = this.pc + 1;
    return true;
  }

  _ld() {
    var op = this.stack[this.sp] & 2047;
    this.stack[this.sp] = this.code[op];
    this.pc = this.pc + 1;
    return true;
  }

  _str() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1] & 2047;
    this.sp = this.sp - 1;
    this.code[op1] = op2;
    this.pc = this.pc + 1;
  }

  _cmp() {
    var op1 = this.stack[this.sp];
    var op2 = this.stack[this.sp - 1];
    this.sp = this.sp - 1;
    if (op1 == op2) {
      this.stack[this.sp] = 1;
    } else {
      this.stack[this.sp] = 0;
    }
    this.pc = this.pc + 1;
    return true;
  }

  _one() {
    this.sp = this.sp + 1;
    this.stack[this.sp] = 1;
    this.pc = this.pc + 1;
    return true;
  }

  _zero() {
    this.sp = this.sp + 1;
    this.stack[this.sp] = 0;
    this.pc = this.pc + 1;
    return true;
  }

  _jump() {
    var op2 = this.stack[this.sp];
    var op1 = this.stack[this.sp - 1];
    this.sp = this.sp - 2;
    this.pc = this.pc + 1;
    if (op2 == 1) {
      this.pc = this.pc + op1;
    }
    return true;
  }

  _putc() {
    var op = this.stack[this.sp];
    this.pc = this.pc + 1;
    print chr(op & 255);
    return true;
  }
  _nop() {
    this.pc = this.pc + 1;
    return true;
  }

  _exit() {
    this.pc = this.pc + 1;
    return false;
  }

  init(flag, code) {
    this.pc = 0;
    this.sp = 0;
    this.code = alloc(2048);
    this.stack = alloc(2048);

    if (code != nil) {
      for(var i = 0; i < len(code); i = i + 1) {
        this.code[i] = code[i];
      }
    }
    if (flag != nil) {
      for (var i = 0; i < len(flag); i = i + 1) {
        this.code[1024 + i] = flag[i];
      }
    }

    this.dispatch = [this._add, this._sub, this._xor, this._copy, this._pop, this._lsl, this._lsr, this._ld, this.str, this._cmp, this._jump, this._one, this._zero, this._putc, this._nop, this._exit];
  }
}

var cpu = CPU();
