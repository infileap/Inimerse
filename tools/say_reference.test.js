'use strict';
const assert = require('node:assert/strict'); const { OutputStream, createRouter, desugarSay } = require('./say_reference');
const events=[]; const router=createRouter({ chat:new OutputStream(e=>events.push(e)) }); const e=router.say('chat','hello',{speaker:'npc'}); assert.equal(e.target,'chat'); assert.equal(events.length,1);
assert.throws(()=>router.say('bad','x'),/unknown/); assert.equal(desugarSay('say@chat "hi"'),'say_target("chat", "hi")');
console.log('say reference tests: ok');
