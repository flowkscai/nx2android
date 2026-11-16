// Included once from gfx_vk_present.c, after barrier() and the swapchain state. The present's anti-aliasing and scaling draws.
// present_pic.h chooses; this builds the pipelines and records the passes. A device that refuses an object once is not asked again.
typedef struct { float vp[4], dst[4], uv[4], flip, sharp, pad0, pad1, out_w, out_h, origin_x, origin_y; } scale_pc;
typedef struct { float vp[4], quality; } aa_pc;
typedef char scale_pc_size_ok[(sizeof(scale_pc) == 80) ? 1 : -1];
typedef struct { VkImage img; VkDeviceMemory mem; VkImageView view; uint32_t w, h; VkFormat fmt; VkImageLayout lay; } picimg_t;
static struct {
    VkDescriptorSetLayout lay1, lay2; VkPipelineLayout pl1, pl2; VkDescriptorPool pool; VkSampler linear, nearest;
    VkPipeline fxaa, edge, blend, neigh, area, easu, rcas, sgsr; VkFormat cfmt, sfmt;
    VkDescriptorSet src[8], aa[8], ed[8], up[8], nb[8];   // nb: SMAA neighbourhood sets (the pipeline is g_filt.neigh)
    picimg_t color[8], edges[8], weight[8], upi[8];
    VkImageView game[8]; VkImage game_img[8]; VkFormat game_fmt[8];
    int dead, ready;
} g_filt;

static void pic_img_free(picimg_t *im) {
    if (im->view) vkDestroyImageView(V.dev, im->view, NULL);
    if (im->img) vkDestroyImage(V.dev, im->img, NULL);
    if (im->mem) vkFreeMemory(V.dev, im->mem, NULL);
    memset(im, 0, sizeof *im);
}
static int pic_img(picimg_t *im, uint32_t w, uint32_t h, VkFormat fmt) {
    if (im->img && im->w == w && im->h == h && im->fmt == fmt) return 1;
    pic_img_free(im);
    if (!w || !h) return 0;
    VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, fmt, {w, h, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r = vkCreateImage(V.dev, &ic, NULL, &im->img);
    VkMemoryRequirements rq = {0}; if (!r) vkGetImageMemoryRequirements(V.dev, im->img, &rq);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, rq.size, vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0)};
    if (!r) r = vkAllocateMemory(V.dev, &ai, NULL, &im->mem);
    if (!r) r = vkBindImageMemory(V.dev, im->img, im->mem, 0);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, im->img, VK_IMAGE_VIEW_TYPE_2D, fmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (!r) r = vkCreateImageView(V.dev, &vi, NULL, &im->view);
    if (r) { pic_img_free(im); return 0; }
    im->w = w; im->h = h; im->fmt = fmt; im->lay = VK_IMAGE_LAYOUT_UNDEFINED;
    return 1;
}
static void pic_to(VkCommandBuffer cb, picimg_t *im, VkImageLayout to) {
    if (!im->img || im->lay == to) return;
    barrier(cb, im->img, im->lay, to); im->lay = to;
}
static void pic_write(VkDescriptorSet set, uint32_t binding, VkSampler samp, VkImageView view, VkImageLayout lay) {
    VkDescriptorImageInfo ii = {samp, view, lay};
    VkWriteDescriptorSet wr = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, binding, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii};
    vkUpdateDescriptorSets(V.dev, 1, &wr, 0, NULL);
}
static int pic_gpl(VkPipeline *pipe, VkPipelineLayout layout, const uint32_t *vs, size_t vn, const uint32_t *fs, size_t fn, VkFormat fmt) {
    if (*pipe) { vkDestroyPipeline(V.dev, *pipe, NULL); *pipe = VK_NULL_HANDLE; }
    VkShaderModule vm = VK_NULL_HANDLE, fm = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vc = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, vn, vs};
    VkShaderModuleCreateInfo fc = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, fn, fs};
    VkResult r = vkCreateShaderModule(V.dev, &vc, NULL, &vm); if (!r) r = vkCreateShaderModule(V.dev, &fc, NULL, &fm);
    VkPipelineShaderStageCreateInfo ss[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vm, "main", NULL},
                                            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main", NULL}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0};
    VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, 0, 0, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, 0, 0, 0, 1};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba = {0, 0, 0, 0, 0, 0, 0, 0xf};
    VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0, 1, &ba};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn};
    VkPipelineRenderingCreateInfo rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &fmt};
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rci, 0, 2, ss, &vi, &ia, NULL, &vps, &rs, &ms, NULL, &cbs, &dy, layout, VK_NULL_HANDLE};
    if (!r) r = vkCreateGraphicsPipelines(V.dev, V.pcache, 1, &ci, NULL, pipe);
    vk_pipeline_cache_changed();
    if (vm) vkDestroyShaderModule(V.dev, vm, NULL); if (fm) vkDestroyShaderModule(V.dev, fm, NULL);
    if (r) { *pipe = VK_NULL_HANDLE; return 0; }
    return 1;
}
static int pic_objs(void) {
    if (g_filt.ready) return 1;
    VkDescriptorSetLayoutBinding b1 = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutBinding b2[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo l1 = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &b1};
    VkDescriptorSetLayoutCreateInfo l2 = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, b2};
    VkPushConstantRange pr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(scale_pc)};
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkSamplerCreateInfo sn = si; sn.magFilter = sn.minFilter = VK_FILTER_NEAREST;
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 56};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 48, 1, &ps};
    VkResult r = vkCreateDescriptorSetLayout(V.dev, &l1, NULL, &g_filt.lay1);
    if (!r) r = vkCreateDescriptorSetLayout(V.dev, &l2, NULL, &g_filt.lay2);
    if (!r) { VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_filt.lay1, 1, &pr}; r = vkCreatePipelineLayout(V.dev, &pl, NULL, &g_filt.pl1); }
    if (!r) { VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_filt.lay2, 1, &pr}; r = vkCreatePipelineLayout(V.dev, &pl, NULL, &g_filt.pl2); }
    if (!r) r = vkCreateSampler(V.dev, &si, NULL, &g_filt.linear);
    if (!r) r = vkCreateSampler(V.dev, &sn, NULL, &g_filt.nearest);
    if (!r) r = vkCreateDescriptorPool(V.dev, &pi, NULL, &g_filt.pool);
    if (!r) {
        VkDescriptorSetLayout ls[40]; for (int i = 0; i < 40; i++) ls[i] = g_filt.lay1;
        VkDescriptorSet all[40];
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_filt.pool, 40, ls};
        r = vkAllocateDescriptorSets(V.dev, &ai, all);
        if (!r) for (int k = 0; k < 8; k++) { g_filt.src[k] = all[k]; g_filt.aa[k] = all[8 + k]; g_filt.ed[k] = all[16 + k]; g_filt.up[k] = all[24 + k]; }
        VkDescriptorSetLayout nls[8]; for (int i = 0; i < 8; i++) nls[i] = g_filt.lay2;
        VkDescriptorSetAllocateInfo ni = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_filt.pool, 8, nls};
        if (!r) r = vkAllocateDescriptorSets(V.dev, &ni, g_filt.nb);
    }
    if (r) { VKRED_ONCE(120, "picture filter: descriptor set, layout, sampler or pool: %d; the blit scales the picture", r); return 0; }
    g_filt.ready = 1; g_filt.cfmt = VK_FORMAT_UNDEFINED; g_filt.sfmt = VK_FORMAT_UNDEFINED;
    return 1;
}
static int pic_pipes(VkFormat cfmt) {
    VkFormat edge = VK_FORMAT_R8G8B8A8_UNORM;
    if (g_filt.cfmt != cfmt) {
        if (!pic_gpl(&g_filt.fxaa, g_filt.pl1, aa_vert_spv, sizeof aa_vert_spv, fxaa_frag_spv, sizeof fxaa_frag_spv, cfmt)) return 0;
        if (!pic_gpl(&g_filt.neigh, g_filt.pl2, aa_vert_spv, sizeof aa_vert_spv, smaa_neigh_frag_spv, sizeof smaa_neigh_frag_spv, cfmt)) return 0;
        g_filt.cfmt = cfmt;
    }
    if (!g_filt.edge && !pic_gpl(&g_filt.edge, g_filt.pl1, aa_vert_spv, sizeof aa_vert_spv, smaa_edge_frag_spv, sizeof smaa_edge_frag_spv, edge)) return 0;
    // Blend weights are signed texel offsets (about -0.5..0.5). The blend pipeline's float target keeps both directions; UNORM would store a negative as 0.
    if (!g_filt.blend && !pic_gpl(&g_filt.blend, g_filt.pl1, aa_vert_spv, sizeof aa_vert_spv, smaa_blend_frag_spv, sizeof smaa_blend_frag_spv, VK_FORMAT_R16G16B16A16_SFLOAT)) return 0;
    if (g_filt.sfmt != g_sfmt) {
        if (!pic_gpl(&g_filt.area, g_filt.pl1, sr_vert_spv, sizeof sr_vert_spv, area_frag_spv, sizeof area_frag_spv, g_sfmt)) return 0;
        if (!pic_gpl(&g_filt.easu, g_filt.pl1, sr_vert_spv, sizeof sr_vert_spv, fsr_easu_frag_spv, sizeof fsr_easu_frag_spv, g_sfmt)) return 0;
        if (!pic_gpl(&g_filt.rcas, g_filt.pl1, sr_vert_spv, sizeof sr_vert_spv, fsr_rcas_frag_spv, sizeof fsr_rcas_frag_spv, g_sfmt)) return 0;
        if (!pic_gpl(&g_filt.sgsr, g_filt.pl1, sr_vert_spv, sizeof sr_vert_spv, sr_frag_spv, sizeof sr_frag_spv, g_sfmt)) return 0;
        g_filt.sfmt = g_sfmt;
    }
    return 1;
}
static int pic_game(uint32_t k, vkimg_t *im) {
    if (g_filt.game[k] && g_filt.game_img[k] == im->img && g_filt.game_fmt[k] == im->fmt) return 1;
    if (g_filt.game[k]) { vkDestroyImageView(V.dev, g_filt.game[k], NULL); g_filt.game[k] = VK_NULL_HANDLE; }
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, im->img, VK_IMAGE_VIEW_TYPE_2D, im->fmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(V.dev, &vi, NULL, &g_filt.game[k])) { g_filt.game_img[k] = VK_NULL_HANDLE; return 0; }
    g_filt.game_img[k] = im->img; g_filt.game_fmt[k] = im->fmt;
    return 1;
}
// The objects for this frame's anti-aliasing and shader scale. 0: present_pic_plan falls back to the bilinear blit.
static int pic_prepare(uint32_t k, vkimg_t *im, int aa, int scale, uint32_t dw, uint32_t dh) {
    if (g_filt.dead) return 0;
    if (!im->w || !im->h || !pic_objs() || !pic_pipes(im->fmt) || !pic_game(k, im)) { g_filt.dead = 1; VKRED_ONCE(121, "picture filter: pipeline or a view of the picture was refused; the blit scales it"); return 0; }
    int fxaa = aa == PIC_AA_FXAA, smaa = aa >= PIC_AA_SMAA_LOW;
    if ((fxaa || smaa) && !pic_img(&g_filt.color[k], im->w, im->h, im->fmt)) { g_filt.dead = 1; VKRED_ONCE(122, "picture filter: the anti-aliasing image; the blit scales the picture"); return 0; }
    if (smaa && (!pic_img(&g_filt.edges[k], im->w, im->h, VK_FORMAT_R8G8B8A8_UNORM) || !pic_img(&g_filt.weight[k], im->w, im->h, VK_FORMAT_R16G16B16A16_SFLOAT))) {
        g_filt.dead = 1; VKRED_ONCE(123, "picture filter: the SMAA images; the blit scales the picture"); return 0; }
    if (scale == PIC_SCALE_FSR && !pic_img(&g_filt.upi[k], dw, dh, g_sfmt)) { g_filt.dead = 1; VKRED_ONCE(124, "picture filter: the FSR image; the blit scales the picture"); return 0; }
    pic_write(g_filt.src[k], 0, g_filt.linear, g_filt.game[k], VK_IMAGE_LAYOUT_GENERAL);
    if (g_filt.color[k].view) pic_write(g_filt.aa[k], 0, g_filt.linear, g_filt.color[k].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (g_filt.edges[k].view) pic_write(g_filt.ed[k], 0, g_filt.nearest, g_filt.edges[k].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (g_filt.upi[k].view) pic_write(g_filt.up[k], 0, g_filt.nearest, g_filt.upi[k].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (g_filt.weight[k].view) {
        pic_write(g_filt.nb[k], 0, g_filt.linear, g_filt.game[k], VK_IMAGE_LAYOUT_GENERAL);
        pic_write(g_filt.nb[k], 1, g_filt.nearest, g_filt.weight[k].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return 1;
}
static void pic_begin(VkCommandBuffer cb, VkImageView view, VkExtent2D ext, int clear) {
    VkClearValue black = {{{0, 0, 0, 1}}};
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED,
        clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE, black};
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, {{0, 0}, ext}, 1, 0, 1, &ca, NULL, NULL};
    vkCmdBeginRendering(cb, &ri);
    VkViewport vp = {0, 0, (float)ext.width, (float)ext.height, 0, 1}; VkRect2D sc = {{0, 0}, ext};
    vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc);
}
static void pic_aa_push(VkCommandBuffer cb, VkPipelineLayout lay, vkimg_t *im, int quality) {
    aa_pc pc = {{1.f / im->w, 1.f / im->h, (float)im->w, (float)im->h}, (float)quality};
    vkCmdPushConstants(cb, lay, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
}
static void pic_aa(VkCommandBuffer cb, uint32_t k, vkimg_t *im, const pic_plan *plan) {
    VkExtent2D ext = {im->w, im->h}; int q = plan->aa_passes == 1 ? 0 : plan->aa - PIC_AA_SMAA_LOW;
    picimg_t *c = &g_filt.color[k];
    if (plan->aa_passes == 1) {
        pic_to(cb, c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        pic_begin(cb, c->view, ext, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.fxaa);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.pl1, 0, 1, &g_filt.src[k], 0, NULL);
        pic_aa_push(cb, g_filt.pl1, im, q); vkCmdDraw(cb, 6, 1, 0, 0); vkCmdEndRendering(cb);
    } else {
        picimg_t *e = &g_filt.edges[k], *w = &g_filt.weight[k];
        pic_to(cb, e, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        pic_begin(cb, e->view, ext, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.edge);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.pl1, 0, 1, &g_filt.src[k], 0, NULL);
        pic_aa_push(cb, g_filt.pl1, im, q); vkCmdDraw(cb, 6, 1, 0, 0); vkCmdEndRendering(cb);
        pic_to(cb, e, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pic_to(cb, w, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        pic_begin(cb, w->view, ext, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.blend);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.pl1, 0, 1, &g_filt.ed[k], 0, NULL);
        pic_aa_push(cb, g_filt.pl1, im, q); vkCmdDraw(cb, 6, 1, 0, 0); vkCmdEndRendering(cb);
        pic_to(cb, w, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pic_to(cb, c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        pic_begin(cb, c->view, ext, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.neigh);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.pl2, 0, 1, &g_filt.nb[k], 0, NULL);
        pic_aa_push(cb, g_filt.pl2, im, q); vkCmdDraw(cb, 6, 1, 0, 0); vkCmdEndRendering(cb);
    }
    pic_to(cb, c, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
static scale_pc pic_scale_pc(vkimg_t *im, int cx, int cy, int cw, int ch, int dx, int dy, int dw, int dh, float sharp, int full) {
    scale_pc pc; memset(&pc, 0, sizeof pc);
    pc.vp[0] = im->w ? 1.f / im->w : 0; pc.vp[1] = im->h ? 1.f / im->h : 0; pc.vp[2] = (float)im->w; pc.vp[3] = (float)im->h;
    if (full) { pc.dst[0] = pc.dst[1] = -1.f; pc.dst[2] = pc.dst[3] = 1.f; }
    else if (g_sext.width && g_sext.height) {
        pc.dst[0] = (float)dx * 2 / g_sext.width - 1; pc.dst[1] = (float)dy * 2 / g_sext.height - 1;
        pc.dst[2] = (float)(dx + dw) * 2 / g_sext.width - 1; pc.dst[3] = (float)(dy + dh) * 2 / g_sext.height - 1;
    }
    if (im->w && im->h) { pc.uv[0] = (float)cx / im->w; pc.uv[1] = (float)cy / im->h; pc.uv[2] = (float)(cx + cw) / im->w; pc.uv[3] = (float)(cy + ch) / im->h; }
    pc.flip = vk_window_bottom_up() ? 1.f : 0.f; pc.sharp = sharp;
    // RCAS addresses EASU's texels from gl_FragCoord, whose pixel centres sit half a texel past the rectangle's origin.
    pc.out_w = (float)dw; pc.out_h = (float)dh; pc.origin_x = (full ? 0.f : (float)dx) + 0.5f; pc.origin_y = (full ? 0.f : (float)dy) + 0.5f;
    return pc;
}
static void pic_draw_scale(VkCommandBuffer cb, VkPipeline pipe, VkDescriptorSet set, const scale_pc *pc) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_filt.pl1, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(cb, g_filt.pl1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof *pc, pc);
    vkCmdDraw(cb, 6, 1, 0, 0);
}
