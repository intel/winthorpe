// Copyright (c) 2014 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
(function() {
  var _listener = null;
  var _callbacks = new Array(256);
  var _dynamic_objects = [];
  var _v8tools = requireNative('v8tools');

  function DBG(msg) {
    console.log('W3CSpeech-D:' + msg);
  }

  function WARN(msg) {
    console.log('W3CSpeech-W:' + msg);
  }

  function ERR(msg) {
    console.log('W3CSpeech-E:' + msg);
  }

  function _getNextReqId() {
    // find the next available request number
    for (var i = 0, max = _callbacks.length; i < max; i++) {
      if (_callbacks[i] == null || _callbacks[i] == undefined)
        return i;
    }
    return i;
  }

  function _sendSyncMessage(msg) {
    return JSON.parse(extension.internal.sendSyncMessage(JSON.stringify(msg)));
  };

  function _postMessage(msg, handler) {
    msg.reqno = _getNextReqId();
    _callbacks[msg.reqno] = handler;

    DBG('Posting message : ' + JSON.stringify(msg));

    try {
      extension.postMessage(JSON.stringify(msg));
    } catch (e) {
      ERR('PostMessage Exception : ' + e);
    }
  };

  function _addConstProperty(obj, prop, propValue) {
    Object.defineProperty(obj, prop, {
      value: propValue == undefined ? null : propValue,
      writable: false,
      configurable: true,
      enumerable: true
    });
  }

  function _addProperty(obj, prop, propValue) {
    Object.defineProperty(obj, prop, {
      value: propValue == undefined ? null : propValue,
      writable: true,
      configurable: true,
      enumerable: true
    });
  }

  function _addPropertyWithSetterGetter(obj, prop, setter, getter) {
    Object.defineProperty(obj, prop, {
      set: setter,
      get: getter,
      configurable: true,
      enumerable: true
    });
  }

  extension.setMessageListener(function(json) {
    var msg = JSON.parse(json);
    var reqno = msg.reqno;

    if (msg.type == 'event') {
      DBG('Got Event : ' + json);
      var args = { };
      var obj = _dynamic_objects[msg.id];
      if (!obj) {
        /* Synthesizer events */
        if (msg.event == 'pending') {
          _v8tools.forceSetProperty(exports, 'pending', msg.state);
        } else if (msg.event == 'speaking') {
          _v8tools.forceSetProperty(exports, 'speaking', msg.state);
        } else if (msg.event == 'paused') {
          _v8tools.forceSetProperty(exports, 'paused', msg.state);
        } else {
          WARN("Unhandled event '" + msg.event + "'");
        }
        return;
      } else if (msg.event == 'audiostart') {
      } else if (msg.event == 'audioend') {
      } else if (msg.event == 'soundstrat') {
      } else if (msg.event == 'soundend') {
      } else if (msg.event == 'speechstart') {
      } else if (msg.event == 'speechend') {
      } else if (msg.event == 'result') {
        // currently we support only signle shot result
        args.resultIndex = 0;
        args.interpretation = null;
        args.emma = null;
        var res = new SpeechRecognitionResult(msg.final);
        msg.results.forEach(function(alternative) {
          res.push({
            'transcript': alternative.transcript,
            'confidence': alternative.confidence
          });
        });
        DBG('Result Length: ' + res.length);
        args.results = [ res ];
      } else if (msg.event == 'nomatch') {
        args.resultIndex = 0;
        args.results = null;
        args.interpretation = null;
        args.emma = null;
      } else if (msg.event == 'error') {
        args.error = msg.error;
        args.message = msg.message;
      } else if (msg.event == 'start') {
        if (obj.type = 'recognizer') {
        } else {
          args.charIndex = msg.charIndex;
          args.elapsedTime = msg.elapsedTime;
          args.name = null;
        }
      } else if (msg.event == 'end') {
        if (obj._type == 'recognizer') {
        } else {
          args.charIndex = msg.charIndex;
          args.elapsedTime = msg.elapsedTime;
          args.name = null;
        }
      } else if (msg.event == 'pause') {
        args.charIndex = msg.charIndex;
        args.elapsedTime = msg.elapsedTime;
        args.name = null;
      } else if (msg.event == 'resume') {
        args.charIndex = msg.charIndex;
        args.elapsedTime = msg.elapsedTime;
        args.name = null;
      } else if (msg.event == 'mark') {
        args.charIndex = msg.charIndex;
        args.elapsedTime = msg.elapsedTime;
        args.name = args.name;
      } else if (msg.event == 'boundary') {
        args.charIndex = msg.charIndex;
        args.elapsedTime = msg.elapsedTime;
        args.name = args.name;
      }

      _dispatchEvent(obj, msg.event, args);
    } else if (reqno != undefined && reqno != null) {
      DBG('Got reply for request no ' + reqno + ': ' + json);
      var handler = _callbacks[reqno];
      delete msg.reqno;
      if (handler != undefined && handler != null) {
        handler(msg);
      }
      return;
    }
  });

  function _GetActiveEventListenerList(et) {
    var a = [];

    for (var type in et._event_listeners) {
      if (et._event_listeners[type].length || et['on' + type]) {
        a.push(type);
      }
    }

    DBG('Events:' + JSON.stringify(a));

    return a;
  }

  function EventTarget(event_types) {
    var _event_listeners = { };
    var _event_handlers = { };
    var self = this;

    if (event_types != null) {
      // initialize event listeners
      event_types.forEach(function(type) {
        _event_listeners[type] = [];
        // Setup a Event Handler
        _addProperty(self, 'on' + type, null);
      });
    }

    this._event_listeners = _event_listeners;
  }

  EventTarget.prototype.isValidEventType = function(type) {
    return type in this._event_listeners;
  };

  EventTarget.prototype.addEventListener = function(type, listener) {
    if (this.isValidEventType(type) &&
        listener != null && typeof listener === 'function' &&
        this._event_listeners[type].indexOf(listener) == -1) {
      this._event_listeners[type].push(listener);
      return;
    }
    ERR('Invalid event type \'' + type + '\', ignoring.' +
        'avaliable event types : ' + this._event_listeners);
  };

  EventTarget.prototype.removeEventListener = function(type, listener) {
    if (!type || !listener)
      return;

    var index = this._event_listeners[type].indexOf(listener);
    if (index == -1)
      return;

    this._event_listeners[type].splice(index, 1);
  };

  EventTarget.prototype.dispatchEvent = function(ev) {
    var handled = true;
    var listeners = null;
    if (typeof ev === 'object' &&
        this.isValidEventType(ev.type) &&
        (listeners = this._event_listeners[ev.type]) != null) {
      listeners.forEach(function(listener) {
        if (!listener(ev) && handled)
          handled = false;
      });
    }
    return handled;
  };

  function _dispatchEvent(obj, type, args) {
    var ev_args = args || { };
    var ev = new CustomEvent(type);
    for (var key in ev_args)
      _addConstProperty(ev, key, ev_args[key]);

    obj.dispatchEvent(ev);

    // call EventHandler attached, if any
    var handler = obj['on' + type];
    if (handler !== null && typeof handler === 'function') {
      handler(ev);
    }
  }

  //
  // SpeechRecognition : Speech -> Text
  //
  function SpeechRecognitionResult(is_final) {

    _addConstProperty(this, 'isFinal', is_final || true);

    this.item = function(index) {
      return this[index];
    };
  }
  SpeechRecognitionResult.prototype = Object.create(Array.prototype);
  SpeechRecognitionResult.prototype.construrctor = SpeechRecognitionResult;

  function SpeechGrammerList() {
    this.push.apply(this, arguments);

    this.item = function(index) {
      return this[index];
    };

    this.toJSON = function() {
      var a = [];
      for (var i = 0; i < this.length; i++) {
        a[i] = this[i];
      }
      return a;
    };
    this.addFromURI = function(src, weight) {
      var w = weight || 1.0;
      this.push({ src: src, weight: w });
    };

    this.addFromString = function(src, weight) {
      this.addFromURI(src, weight);
    };
  }
  SpeechGrammerList.prototype = Object.create(Array.prototype);
  SpeechGrammerList.prototype.constructor = SpeechGrammerList;

  function SpeechRecognition() {
    this._id = -1;
    this._is_active = 0;
    this._type = 'recognizer';

    EventTarget.call(this, [
      'audiostart',
      'soundstart',
      'speechstart',
      'speechend',
      'soundend',
      'audioend',
      'result',
      'nomatch',
      'error',
      'start',
      'end']);
    _addProperty(this, 'grammars', new SpeechGrammerList());
    _addProperty(this, 'lang');
    _addProperty(this, 'continuous', false);
    _addProperty(this, 'interimResults', 1);
    _addProperty(this, 'maxAlternatives', 1);
    _addProperty(this, 'serviceURI');

    var sr = this;

    this.start = function() {
      if (!this.onresult && !this._event_listeners['result'].length) {
        WARN("No handler 'result' handler set!!! no use of this recognizeri" +
             'unless handle the results');
      }

      DBG('About to start speech recognition with lang ' + this.lang +
          ', grammars: ' + this.grammars);

      var reply = _sendSyncMessage({
        'reqno': _getNextReqId(),
        'type': 'create',
        'object': 'recognizer',
        'set': {
          'grammars': this.grammars,
          'lang': this.lang,
          'continuous': this.continuous,
          'events': _GetActiveEventListenerList(this)
        }
      });

      if (reply.error) {
        WARN('Failed to create speech recognizer: ' + reply.message);
        _dispatchEvent(sr, 'error', {
          'error': reply.error,
          'message': reply.message
        });
        return;
      }
      this._id = reply.id;
      _dynamic_objects[reply.id] = this;

      DBG('Rcognizer ID : ' + this._id);
      _postMessage({
        'type': 'invoke',
        'method': 'start',
        'id': this._id
      }, function(reply) {
        if (reply.error) {
          sr._is_active = false;
          WARN('Failed to start speech recognizer: ' + reply.message);
          _dispatchEvent(sr, 'error', {
            'error': reply.error,
            'message': reply.message
          });
        } else
          DBG('Start method status: ' + reply.status);
      });

      this._is_active = true;
    };

    this.stop = function() {
      if (!this._is_active) {
        DBG('Recognition has not yet started.');
        return;
      }

      _postMessage({
        'type': 'invoke',
        'method': 'stop',
        'id': this._id
      }, function(reply) {
        if (reply.error) {
          sr._is_active = true;
          WARN('Failed to stop speech recognizer: ' + reply.message);
          _dispatchEvent(sr, 'error', {
            'error': reply.error,
            'message': reply.message
          });
        }
      });

      this._is_active = false;
    };

    this.abort = function() {
      if (!this._is_active)
        return;

      _postMessage({
        'type': 'invoke',
        'method': 'abort',
        'id': this._id
      }, function(reply) {
        if (reply.error) {
          WARN('Failed to abort: ' + reply.message);
        }
      });

      this._is_active = false;
    };
  }
  SpeechRecognition.prototype = Object.create(EventTarget.prototype);
  //
  // SpeechSynthesis (Text -> Speech)
  //

  function SpeechSynthesisUtterance(text) {
    this._id = -1;
    var _text = text ? text: '';
    var _lang = 'english';
    var _voiceURI = null;
    var _volume = 1.0;
    var _rate = 0.0;
    var _pitch = 1.0;

    EventTarget.call(this, [
      'start',
      'end',
      'error',
      'pause',
      'resume',
      'mark',
      'boundary'
    ]);

    var ut = this;

    function setAttribute(name, value) {
      if (ut._id == -1) {
        DBG('Ignoring setAttribute request as no id preset for this object');
        return;
      }
      var set = {};
      set[name] = value;
      _sendSyncMessage({
        'reqno': _getNextReqId(),
        'type': 'set',
        'id': ut._id,
        'set': set
      });
    }

    _addPropertyWithSetterGetter(this, 'text', function(text) {
      _text = text;
      setAttribute('text', _text);
    }, function() { return _text; });
    _addPropertyWithSetterGetter(this, 'lang', function(lang) {
      _lang = lang;
      setAttribute('lang', lang);
    }, function() { return _lang; });
    _addPropertyWithSetterGetter(this, 'voiceURI', function(uri) {
      _voiceURI = uri;
      setAttribute('voiceURI', uri);
    }, function() { return _voiceURI; });
    _addPropertyWithSetterGetter(this, 'volume', function(volume) {
      _volume = volume;
      setAttribute('volume', volume);
    }, function() { return _volume; });
    _addPropertyWithSetterGetter(this, 'rate', function(rate) {
      _rate = rate;
      setAttribute('rate', rate);
    }, function() { return _rate; });
    _addPropertyWithSetterGetter(this, 'pitch', function(pitch) {
      _pitch = pitch;
      setAttribute('pitch', pitch);
    }, function() { return _pitch; });


    this.addEventListener('end', function() {
      _dynamic_objects[ut._id] = null;
    });
    this.addEventListener('error', function() {
      _dynamic_objects[ut._id] = null;
    });

    var reply = _sendSyncMessage({
      'reqno': _getNextReqId(),
      'type': 'create',
      'object': 'utterance',
      'set': {
        'text': this.text,
        'pitch': this.pitch,
        'rate': this.rate,
        'volume': this.volume,
        'lang': this.lang,
        'events': _GetActiveEventListenerList(this)
      }
    });
    if (reply.error) {
      WARN('Failed to create uttenrance object: ' + reply.message);
      return;
    }

    DBG("Utterance id : " + reply.id);

    this._id = reply.id;
    _dynamic_objects[reply.id] = this;
  }
  SpeechSynthesisUtterance.prototype = Object.create(EventTarget.prototype);
  SpeechSynthesisUtterance.prototype.constructor = SpeechSynthesisUtterance;

  function SpeechSynthesisVoice(info) {
    _addConstProperty('voiceURI', info.uri);
    _addConstProperty('name', info.name);
    _addConstProperty('lang', info.lang);
    _addConstProperty('localService', info.locaService);
    _addConstProperty('default', info.default);
  }

  function SpeechSynthesisVoiceList() {
    this.push.apply(this, arguments);

    this.item = function(index) {
      return this[index];
    };

    this.toJSON = function() {
      var a = [];
      for (var i = 0; i < this.length; i++) {
        a[i] = this[i];
      }
      return a;
    };
  }
  SpeechSynthesisVoiceList.prototype = Object.create(Array.prototype);
  SpeechSynthesisVoiceList.prototype.construrctor = SpeechSynthesisVoiceList;

  function SpeechSynthesis() {
    _addConstProperty(this, 'pending', false);
    _addConstProperty(this, 'speaking', false);
    _addConstProperty(this, 'paused', false);

    var sy = this;

    this.speak = function(utterance) {
      if (utterance._id == -1) {
        WARN('Not a valid utternace');
        _dispatchEvent(utterance, 'error', {
          'error': 'network',
          'message': 'failed to connect to server'
        });
        return;
      }

      DBG("Sending speak request for text '" + utterance.text + "'");
      _postMessage({
        'id': 0,
        'type': 'invoke',
        'method': 'speak',
        'utterance': utterance._id
      }, function(reply) {
        if (reply.error) {
          WARN('Failed to synthesize, error: ' + reply.message);
          _dispatchEvent(utterance, 'error', {
            'error': reply.error,
            'message': reply.message
          });
          return;
        }
      });
    };

    this.cancel = function() {
      _postMessage({
        'id': 0,
        'type': 'invoke',
        'method': 'cancel'
      }, function(reply) {
      });
    };

    this.pause = function() {
      _postMessage({
        'id': 0,
        'type': 'invoke',
        'method': 'pause'
      }, function(reply) {
      });
    };

    this.resume = function() {
      _postMessage({
        'id': 0,
        'type': 'invoke',
        'method': 'resume'
      }, function(reply) {
      });
    };

    this.getVoices = function() {
      var voices = new SpeechSynthesisVoiceList();
      var reply = _sendSyncMessage({
        'id': 0,
        'type': 'invoke',
        'method': 'getVoices'
      });

      if (reply.status != 0) {
        WARN('Failed to get voices, error: ' + reply.message);
      } else {
      }

      return voices;
    };
  }
  tizen.SpeechRecognition = SpeechRecognition;
  tizen.SpeechSynthesisUtterance = SpeechSynthesisUtterance;
  exports = new SpeechSynthesis();
})();
