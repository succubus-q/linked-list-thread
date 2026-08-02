#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>


// Один элемент двусвязного списка
typedef struct Node {
    uint32_t value;
    struct Node *prev;
    struct Node *next;
} Node;


// Двусвязный список
typedef struct List {
    Node *head;
    Node *tail;

    // Мьютекс защищает голову, хвост и связи между узлами
    pthread_mutex_t mutex;
} List;


// Возможные направления движения по списку
typedef enum Direction {
    FROM_HEAD,
    FROM_TAIL
} Direction;


// Какие биты должен считать поток
typedef enum BitType {
    ZERO_BITS,
    ONE_BITS
} BitType;


// Данные и результаты одного рабочего потока
typedef struct WorkerData {
    List *list;

    Direction direction;
    BitType bit_type;

    size_t processed;
    unsigned long long bit_count;
} WorkerData;


/*
 * Получение случайного 32-битного значения.
 *
 * rand() может выдавать меньше 32 случайных битов,
 * поэтому объединяем результаты двух вызовов.
 */
uint32_t random_uint32(void)
{
    uint32_t first_part = (uint32_t)rand();
    uint32_t second_part = (uint32_t)rand();

    return (first_part << 16) ^ second_part;
}


// Добавление элемента в конец списка
int push_back(List *list, uint32_t value)
{
    Node *new_node = malloc(sizeof *new_node);

    if (new_node == NULL) {
        return 0;
    }

    new_node->value = value;
    new_node->prev = list->tail;
    new_node->next = NULL;

    if (list->head == NULL) {
        // Новый узел одновременно становится головой
        list->head = new_node;
    } else {
        // Старый хвост ссылается на новый узел
        list->tail->next = new_node;
    }

    // Новый узел становится хвостом
    list->tail = new_node;

    return 1;
}


// Отсоединение узла с головы
Node *take_front(List *list)
{
    pthread_mutex_lock(&list->mutex);

    if (list->head == NULL) {
        pthread_mutex_unlock(&list->mutex);
        return NULL;
    }

    Node *removed = list->head;

    if (list->head == list->tail) {
        // В списке остался один узел
        list->head = NULL;
        list->tail = NULL;
    } else {
        list->head = removed->next;
        list->head->prev = NULL;
    }

    // Полностью отсоединяем узел от списка
    removed->prev = NULL;
    removed->next = NULL;

    pthread_mutex_unlock(&list->mutex);

    return removed;
}


// Отсоединение узла с хвоста
Node *take_back(List *list)
{
    pthread_mutex_lock(&list->mutex);

    if (list->tail == NULL) {
        pthread_mutex_unlock(&list->mutex);
        return NULL;
    }

    Node *removed = list->tail;

    if (list->head == list->tail) {
        // В списке остался один узел
        list->head = NULL;
        list->tail = NULL;
    } else {
        list->tail = removed->prev;
        list->tail->next = NULL;
    }

    // Полностью отсоединяем узел от списка
    removed->prev = NULL;
    removed->next = NULL;

    pthread_mutex_unlock(&list->mutex);

    return removed;
}


// Подсчёт единичных битов в 32-битном числе
unsigned int count_one_bits(uint32_t value)
{
    unsigned int one_bits = 0;

    for (unsigned int bit = 0; bit < 32; bit++) {
        if ((value & 1U) == 1U) {
            one_bits++;
        }

        value >>= 1;
    }

    return one_bits;
}


// Подсчёт нулевых битов в 32-битном числе
unsigned int count_zero_bits(uint32_t value)
{
    unsigned int zero_bits = 0;

    for (unsigned int bit = 0; bit < 32; bit++) {
        if ((value & 1U) == 0U) {
            zero_bits++;
        }

        value >>= 1;
    }

    return zero_bits;
}


// Единая функция для обоих потоков
void *worker(void *argument)
{
    WorkerData *data = argument;

    while (1) {
        Node *node;

        /*
         * Направление работы определяется значением,
         * которое было передано через WorkerData.
         */
        if (data->direction == FROM_HEAD) {
            node = take_front(data->list);
        } else {
            node = take_back(data->list);
        }

        // Список закончился
        if (node == NULL) {
            break;
        }

        /*
         * Тип подсчитываемых битов также определяется
         * аргументами потока.
         */
        if (data->bit_type == ZERO_BITS) {
            data->bit_count += count_zero_bits(node->value);
        } else {
            data->bit_count += count_one_bits(node->value);
        }

        data->processed++;

        // Узел обработан, поэтому освобождаем память
        free(node);
    }

    return NULL;
}


// Освобождение оставшихся элементов списка
void list_destroy(List *list)
{
    Node *current = list->head;

    while (current != NULL) {
        Node *next = current->next;

        free(current);
        current = next;
    }

    list->head = NULL;
    list->tail = NULL;
}


int main(void)
{
    size_t element_count;

    printf("Введите количество элементов списка: ");

    if (scanf("%zu", &element_count) != 1) {
        printf("Ошибка: необходимо ввести целое неотрицательное число\n");
        return 1;
    }

    List list = {
        .head = NULL,
        .tail = NULL
    };

    int mutex_result = pthread_mutex_init(&list.mutex, NULL);

    if (mutex_result != 0) {
        printf("Не удалось создать мьютекс\n");
        return 1;
    }

    // Инициализируем генератор случайных чисел текущим временем
    srand((unsigned int)time(NULL));

    for (size_t index = 0; index < element_count; index++) {
        uint32_t value = random_uint32();

        if (!push_back(&list, value)) {
            printf("Ошибка выделения памяти\n");

            list_destroy(&list);
            pthread_mutex_destroy(&list.mutex);

            return 1;
        }
    }

    /*
     * Данные первого потока:
     * брать узлы с головы и считать нулевые биты.
     */
    WorkerData head_data = {
        .list = &list,
        .direction = FROM_HEAD,
        .bit_type = ZERO_BITS,
        .processed = 0,
        .bit_count = 0
    };

    /*
     * Данные второго потока:
     * брать узлы с хвоста и считать единичные биты.
     */
    WorkerData tail_data = {
        .list = &list,
        .direction = FROM_TAIL,
        .bit_type = ONE_BITS,
        .processed = 0,
        .bit_count = 0
    };

    pthread_t head_thread;
    pthread_t tail_thread;

    /*
     * Оба потока запускают одну и ту же функцию worker.
     * Отличается только переданная структура WorkerData.
     */
    int head_result = pthread_create(
        &head_thread,
        NULL,
        worker,
        &head_data
    );

    if (head_result != 0) {
        printf("Не удалось создать поток с головы\n");

        list_destroy(&list);
        pthread_mutex_destroy(&list.mutex);

        return 1;
    }

    int tail_result = pthread_create(
        &tail_thread,
        NULL,
        worker,
        &tail_data
    );

    if (tail_result != 0) {
        printf("Не удалось создать поток с хвоста\n");

        // Поток с головы уже запущен, поэтому ждём его
        pthread_join(head_thread, NULL);

        list_destroy(&list);
        pthread_mutex_destroy(&list.mutex);

        return 1;
    }

    // Главный поток ждёт завершения обоих рабочих потоков
    pthread_join(head_thread, NULL);
    pthread_join(tail_thread, NULL);

    size_t total_processed =
        head_data.processed + tail_data.processed;

    printf("\nРезультаты потока с головы:\n");
    printf("  обработано элементов: %zu\n", head_data.processed);
    printf("  найдено нулевых битов: %llu\n", head_data.bit_count);

    printf("\nРезультаты потока с хвоста:\n");
    printf("  обработано элементов: %zu\n", tail_data.processed);
    printf("  найдено единичных битов: %llu\n", tail_data.bit_count);

    printf("\nВсего обработано элементов: %zu\n", total_processed);
    printf("Изначально элементов: %zu\n", element_count);

    if (total_processed == element_count) {
        printf("Каждый элемент обработан один раз\n");
    } else {
        printf("Ошибка: количество обработанных элементов не совпало\n");
    }

    /*
     * После штатной работы список уже пуст.
     * Функция освободит память, только если что-то осталось.
     */
    list_destroy(&list);

    pthread_mutex_destroy(&list.mutex);

    return 0;
}