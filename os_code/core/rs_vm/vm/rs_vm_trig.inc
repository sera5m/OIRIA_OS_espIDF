/* Included from rsvm_step. LUT + uint32 wrap (2^32 ticks = 2\u03c0). */
    case RSVM_OP_SIN:
    case RSVM_OP_COS:
    case RSVM_OP_TAN: {
        rsvm_val_t a = pop(vm);
        uint32_t ph;
        if (is_float_ty(a.ty)) {
            int32_t q16 = (int32_t)(as_f32(a) * 10430.378f);
            ph = ((uint32_t)q16) << 16;
        } else {
            ph = (uint32_t)((int32_t)a.v * 11930465);
        }
        int16_t q15;
        if (op == RSVM_OP_COS) q15 = pcm_cos_i16_lerp(ph);
        else if (op == RSVM_OP_TAN) q15 = pcm_tan_i16(ph);
        else q15 = pcm_sin_i16_lerp(ph);
        if (is_float_ty(a.ty))
            push(vm, V_f32((float)q15 * (1.0f / 32767.0f)));
        else
            push(vm, V_i32((int32_t)q15));
        break;
    }
    case RSVM_OP_SIN_AMP: {
        rsvm_val_t amp = pop(vm);
        rsvm_val_t a = pop(vm);
        uint32_t ph = is_float_ty(a.ty)
            ? (((uint32_t)(int32_t)(as_f32(a) * 10430.378f)) << 16)
            : (uint32_t)((int32_t)a.v * 11930465);
        int32_t s = pcm_sin_i16_lerp(ph);
        push(vm, V_i32((s * as_i32(amp)) >> 15));
        break;
    }
