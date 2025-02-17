/*
 * Copyright (c) 2021, the SerenityOS developers.
 * Copyright (c) 2022, Sam Atkins <atkinssj@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibCore/Stream.h>
#include <LibCore/System.h>
#include <LibMain/Main.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define COL1_COLOR "\x1B[32m{}\x1B[0m"
#define COL2_COLOR "\x1B[34m{}\x1B[0m"
#define COL3_COLOR "\x1B[31m{}\x1B[0m"

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio rpath"));

    String file1_path;
    String file2_path;
    bool suppress_col1 { false };
    bool suppress_col2 { false };
    bool suppress_col3 { false };
    bool case_insensitive { false };
    bool color { false };
    bool no_color { false };
    bool print_total { false };

    Core::ArgsParser args_parser;
    args_parser.set_general_help("Compare two sorted files line by line");
    args_parser.add_option(suppress_col1, "Suppress column 1 (lines unique to file1)", nullptr, '1');
    args_parser.add_option(suppress_col2, "Suppress column 2 (lines unique to file2)", nullptr, '2');
    args_parser.add_option(suppress_col3, "Suppress column 3 (lines common to both files)", nullptr, '3');
    args_parser.add_option(case_insensitive, "Use case-insensitive comparison of lines", nullptr, 'i');
    args_parser.add_option(color, "Always print colored output", "color", 'c');
    args_parser.add_option(no_color, "Do not print colored output", "no-color", 0);
    args_parser.add_option(print_total, "Print a summary", "total", 't');
    args_parser.add_positional_argument(file1_path, "First file to compare", "file1");
    args_parser.add_positional_argument(file2_path, "Second file to compare", "file2");
    args_parser.parse(arguments);

    if (color && no_color) {
        warnln("Cannot specify 'color' and 'no-color' together");
        return 1;
    }

    bool print_color = TRY(Core::System::isatty(STDOUT_FILENO));
    if (color)
        print_color = true;
    else if (no_color)
        print_color = false;

    if (file1_path == "-" && file2_path == "-") {
        warnln("File1 and file2 cannot both be the standard input");
        return 1;
    }

    auto open_file = [](String const& path, auto& file, int file_number) {
        auto file_or_error = Core::Stream::File::open_file_or_standard_stream(path, Core::Stream::OpenMode::Read);
        if (file_or_error.is_error()) {
            warnln("Failed to open file{} '{}': {}", file_number, path, file_or_error.error());
            return false;
        }

        if (path != "-" && Core::File::is_directory(path)) {
            warnln("Failed to open file{} '{}': is a directory", file_number, path);
            return false;
        }

        auto buffered_file_or_error = Core::Stream::BufferedFile::create(file_or_error.release_value());
        if (buffered_file_or_error.is_error()) {
            warnln("Failed to create buffer for file{} '{}': {}", file_number, path, buffered_file_or_error.error());
            return false;
        }

        file = buffered_file_or_error.release_value();
        return true;
    };

    OwnPtr<Core::Stream::BufferedFile> file1;
    OwnPtr<Core::Stream::BufferedFile> file2;
    if (!(open_file(file1_path, file1, 1) && open_file(file2_path, file2, 2)))
        return 1;

    char tab { '\t' };
    size_t tab_count { 0 };
    String col1_fmt;
    String col2_fmt;
    String col3_fmt;
    if (!suppress_col1)
        col1_fmt = String::formatted("{}{}", String::repeated(tab, tab_count++), print_color ? COL1_COLOR : "{}");
    if (!suppress_col2)
        col2_fmt = String::formatted("{}{}", String::repeated(tab, tab_count++), print_color ? COL2_COLOR : "{}");
    if (!suppress_col3)
        col3_fmt = String::formatted("{}{}", String::repeated(tab, tab_count++), print_color ? COL3_COLOR : "{}");

    auto cmp = [&](String const& str1, String const& str2) {
        if (case_insensitive)
            return strcasecmp(str1.characters(), str2.characters());
        return strcmp(str1.characters(), str2.characters());
    };

    bool read_file1 { true };
    bool read_file2 { true };
    int col1_count { 0 };
    int col2_count { 0 };
    int col3_count { 0 };
    String file1_line;
    String file2_line;
    Array<u8, PAGE_SIZE> buffer;

    auto should_continue_comparing_files = [&]() {
        if (read_file1) {
            auto can_read_file1_line = file1->can_read_line();
            if (can_read_file1_line.is_error() || !can_read_file1_line.value())
                return false;
        }
        if (read_file2) {
            auto can_read_file2_line = file2->can_read_line();
            if (can_read_file2_line.is_error() || !can_read_file2_line.value())
                return false;
        }
        return true;
    };

    while (should_continue_comparing_files()) {
        if (read_file1)
            file1_line = TRY(file1->read_line(buffer));
        if (read_file2)
            file2_line = TRY(file2->read_line(buffer));

        int cmp_result = cmp(file1_line, file2_line);

        if (cmp_result == 0) {
            ++col3_count;
            read_file1 = read_file2 = true;
            if (!suppress_col3)
                outln(col3_fmt, file1_line);
        } else if (cmp_result < 0) {
            ++col1_count;
            read_file1 = true;
            read_file2 = false;
            if (!suppress_col1)
                outln(col1_fmt, file1_line);
        } else {
            ++col2_count;
            read_file1 = false;
            read_file2 = true;
            if (!suppress_col2)
                outln(col2_fmt, file2_line);
        }
    }

    // If the most recent line read was not a match, then the last line read from one of the files has not yet been output.
    // So let's output it!
    if (!read_file1 && !suppress_col1) {
        ++col1_count;
        outln(col1_fmt, file1_line);
    } else if (!read_file2 && !suppress_col2) {
        ++col2_count;
        outln(col2_fmt, file2_line);
    }

    auto process_remaining = [&](String const& fmt, auto& file, int& count, bool print) {
        while (true) {
            auto can_read_result = file->can_read_line();
            if (can_read_result.is_error() || !can_read_result.value())
                break;
            ++count;
            auto line = file->read_line(buffer);
            if (line.is_error())
                break;
            if (print)
                outln(fmt, line.value());
        }
    };
    process_remaining(col1_fmt, file1, col1_count, !suppress_col1);
    process_remaining(col2_fmt, file2, col2_count, !suppress_col2);

    if (print_total)
        outln(print_color ? COL1_COLOR "\t" COL2_COLOR "\t" COL3_COLOR "\ttotal"sv : "{}\t{}\t{}\ttotal"sv, col1_count, col2_count, col3_count);

    return 0;
}
