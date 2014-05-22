/*
 * Copyright (c) 2012 - 2014, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>

#include "srs/daemon/iso-6391.h"


/*
 * table of known ISO 639-1 language codes
 */

static srs_iso6391_t iso6391_codes[] = {
    { "aa", "Afar" },
    { "ab", "Abkhazian" },
    { "ae", "Avestan" },
    { "af", "Afrikaans" },
    { "ak", "Akan" },
    { "am", "Amharic" },
    { "an", "Aragonese" },
    { "ar", "Arabic" },
    { "as", "Assamese" },
    { "av", "Avaric" },
    { "ay", "Aymara" },
    { "az", "Azerbaijani" },
    { "ba", "Bashkir" },
    { "be", "Belarusian" },
    { "bg", "Bulgarian" },
    { "bh", "Bihari languages" },
    { "bi", "Bislama" },
    { "bm", "Bambara" },
    { "bn", "Bengali" },
    { "bo", "Tibetan" },
    { "br", "Breton" },
    { "bs", "Bosnian" },
    { "ca", "Catalan" },
    { "ce", "Chechen" },
    { "ch", "Chamorro" },
    { "co", "Corsican" },
    { "cr", "Cree" },
    { "cs", "Czech" },
    { "cu", "Church Slavic" },
    { "cv", "Chuvash" },
    { "cy", "Welsh" },
    { "da", "Danish" },
    { "de", "German" },
    { "dv", "Dhivehi" },
    { "dz", "Dzongkha" },
    { "ee", "Ewe" },
    { "el", "Modern Greek" },
    { "en", "English" },
    { "eo", "Esperanto" },
    { "es", "Spanish" },
    { "et", "Estonian" },
    { "eu", "Basque" },
    { "fa", "Persian" },
    { "ff", "Fulah" },
    { "fi", "Finnish" },
    { "fj", "Fijian" },
    { "fo", "Faroese" },
    { "fr", "French" },
    { "fy", "Western Frisian" },
    { "ga", "Irish" },
    { "gd", "Scottish Gaelic" },
    { "gl", "Galician" },
    { "gn", "Guarani" },
    { "gu", "Gujarati" },
    { "gv", "Manx" },
    { "ha", "Hausa" },
    { "he", "Hebrew" },
    { "hi", "Hindi" },
    { "ho", "Hiri Motu" },
    { "hr", "Croatian" },
    { "ht", "Haitian" },
    { "hu", "Hungarian" },
    { "hy", "Armenian" },
    { "hz", "Herero" },
    { "ia", "Interlingua" },
    { "id", "Indonesian" },
    { "ie", "Interlingue" },
    { "ig", "Igbo" },
    { "ii", "Sichuan Yi" },
    { "ik", "Inupiaq" },
    { "io", "Ido" },
    { "is", "Icelandic" },
    { "it", "Italian" },
    { "iu", "Inuktitut" },
    { "ja", "Japanese" },
    { "jv", "Javanese" },
    { "ka", "Georgian" },
    { "kg", "Kongo" },
    { "ki", "Kikuyu" },
    { "kj", "Kuanyama" },
    { "kk", "Kazakh" },
    { "kl", "Kalaallisut" },
    { "km", "Central Khmer" },
    { "kn", "Kannada" },
    { "ko", "Korean" },
    { "kr", "Kanuri" },
    { "ks", "Kashmiri" },
    { "ku", "Kurdish" },
    { "kv", "Komi" },
    { "kw", "Cornish" },
    { "ky", "Kirghiz" },
    { "la", "Latin" },
    { "lb", "Luxembourgish" },
    { "lg", "Ganda" },
    { "li", "Limburgan" },
    { "ln", "Lingala" },
    { "lo", "Lao" },
    { "lt", "Lithuanian" },
    { "lu", "Luba-Katanga" },
    { "lv", "Latvian" },
    { "mg", "Malagasy" },
    { "mh", "Marshallese" },
    { "mi", "Maori" },
    { "mk", "Macedonian" },
    { "ml", "Malayalam" },
    { "mn", "Mongolian" },
    { "mr", "Marathi" },
    { "ms", "Malay" },
    { "mt", "Maltese" },
    { "my", "Burmese" },
    { "na", "Nauru" },
    { "nb", "Norwegian Bokmål" },
    { "nd", "North Ndebele" },
    { "ne", "Nepali" },
    { "ng", "Ndonga" },
    { "nl", "Dutch" },
    { "nn", "Norwegian Nynorsk" },
    { "no", "Norwegian" },
    { "nr", "South Ndebele" },
    { "nv", "Navajo" },
    { "ny", "Nyanja" },
    { "oc", "Occitan" },
    { "oj", "Ojibwa" },
    { "om", "Oromo" },
    { "or", "Oriya" },
    { "os", "Ossetian" },
    { "pa", "Panjabi" },
    { "pi", "Pali" },
    { "pl", "Polish" },
    { "ps", "Pushto" },
    { "pt", "Portuguese" },
    { "qu", "Quechua" },
    { "rm", "Romansh" },
    { "rn", "Rundi" },
    { "ro", "Romanian" },
    { "ru", "Russian" },
    { "rw", "Kinyarwanda" },
    { "sa", "Sanskrit" },
    { "sc", "Sardinian" },
    { "sd", "Sindhi" },
    { "se", "Northern Sami" },
    { "sg", "Sango" },
    { "sh", "Serbo-Croatian" },
    { "si", "Sinhala" },
    { "sk", "Slovak" },
    { "sl", "Slovenian" },
    { "sm", "Samoan" },
    { "sn", "Shona" },
    { "so", "Somali" },
    { "sq", "Albanian" },
    { "sr", "Serbian" },
    { "ss", "Swati" },
    { "st", "Southern Sotho" },
    { "su", "Sundanese" },
    { "sv", "Swedish" },
    { "sw", "Swahili" },
    { "ta", "Tamil" },
    { "te", "Telugu" },
    { "tg", "Tajik" },
    { "th", "Thai" },
    { "ti", "Tigrinya" },
    { "tk", "Turkmen" },
    { "tl", "Tagalog" },
    { "tn", "Tswana" },
    { "to", "Tonga" },
    { "tr", "Turkish" },
    { "ts", "Tsonga" },
    { "tt", "Tatar" },
    { "tw", "Twi" },
    { "ty", "Tahitian" },
    { "ug", "Uighur" },
    { "uk", "Ukrainian" },
    { "ur", "Urdu" },
    { "uz", "Uzbek" },
    { "ve", "Venda" },
    { "vi", "Vietnamese" },
    { "vo", "Volapük" },
    { "wa", "Walloon" },
    { "wo", "Wolof" },
    { "xh", "Xhosa" },
    { "yi", "Yiddish" },
    { "yo", "Yoruba" },
    { "za", "Zhuang" },
    { "zh", "Chinese" },
    { "zu", "Zulu" },

    /* These are added as exceptions for espeak. */
    { "grc", "Ancient Greek" },
    { "jbo", "Lojban" },

    { NULL, NULL }
};


static srs_dialect_t dialects[] = {
    { "br", "Brazilian" },
    { "pt", "Portugese" },
    { "fr", "French" },
    { "be", "Belgian" },
    { "es", "Spanish" },
    { "la", "Latin American" },
    { "gb", "British" },
    { "uk", "British" },
    { "us", "American" },
    { "sc", "Scottish" },
    { "wi", "West Indies" },
    { "pin", "Pinglish" },
    { NULL, NULL },
};


const char *srs_iso6391_language(const char *code)
{
    srs_iso6391_t *c;

    for (c = iso6391_codes; c->code != NULL; c++)
        if (!strcmp(c->code, code))
            return c->language;

    return NULL;
}


const char *srs_iso6391_dialect(const char *code)
{
    srs_dialect_t *d;

    for (d = dialects; d->code != NULL; d++)
        if (!strcmp(d->code, code))
            return d->dialect;

    return NULL;
}
